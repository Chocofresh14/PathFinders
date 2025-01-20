#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_URL_LENGTH 2048
#define MAX_LINE_LENGTH 256
#define MAX_THREADS 10

// Structure pour stocker les données de réponse HTTP
struct MemoryStruct {
    char *memory;
    size_t size;
};

// Structure pour passer les paramètres aux threads
struct ThreadData {
    char *base_url;
    char *wordlist_path;
    int current_level;
    int max_level;
    FILE *output_file;
    pthread_mutex_t output_mutex;
    pthread_mutex_t curl_mutex;
};

// File d'attente pour les URLs à scanner
struct Queue {
    char **urls;
    int front;
    int rear;
    int size;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

// Variables globales
struct Queue *url_queue;
int active_threads = 0;
pthread_mutex_t active_threads_mutex = PTHREAD_MUTEX_INITIALIZER;

// Fonction de callback pour CURL
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("Erreur: Échec de l'allocation mémoire\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Initialisation de la file d'attente
struct Queue* createQueue(int capacity) {
    struct Queue *queue = (struct Queue*)malloc(sizeof(struct Queue));
    if (!queue) {
        return NULL;
    }
    
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->urls = (char**)malloc(capacity * sizeof(char*));
    
    if (!queue->urls) {
        free(queue);
        return NULL;
    }

    for (int i = 0; i < capacity; i++) {
        queue->urls[i] = (char*)malloc(MAX_URL_LENGTH * sizeof(char));
        if (!queue->urls[i]) {
            for (int j = 0; j < i; j++) {
                free(queue->urls[j]);
            }
            free(queue->urls);
            free(queue);
            return NULL;
        }
        queue->urls[i][0] = '\0';
    }

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
    return queue;
}

// Ajouter une URL à la file d'attente
void enqueue(struct Queue *queue, const char *url) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->size == queue->capacity) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    
    queue->rear = (queue->rear + 1) % queue->capacity;
    strncpy(queue->urls[queue->rear], url, MAX_URL_LENGTH - 1);
    queue->urls[queue->rear][MAX_URL_LENGTH - 1] = '\0';
    queue->size++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
}

// Récupérer une URL de la file d'attente
int dequeue(struct Queue *queue, char *url) {
    pthread_mutex_lock(&queue->mutex);
    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }
    
    strncpy(url, queue->urls[queue->front], MAX_URL_LENGTH - 1);
    url[MAX_URL_LENGTH - 1] = '\0';
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

// Fonction principale de scan exécutée par chaque thread
void* scan_worker(void *arg) {
    struct ThreadData *data = (struct ThreadData*)arg;
    CURL *curl;
    char url[MAX_URL_LENGTH];
    
    pthread_mutex_lock(&data->curl_mutex);
    curl = curl_easy_init();
    pthread_mutex_unlock(&data->curl_mutex);
    
    if (!curl) {
        return NULL;
    }

    while (1) {
        if (!dequeue(url_queue, url)) {
            break;
        }

        struct MemoryStruct chunk = {0};
        chunk.memory = malloc(1);
        if (!chunk.memory) {
            continue;
        }
        chunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200) {
                pthread_mutex_lock(&data->output_mutex);
                printf("[TROUVÉ] %s\n", url);
                if (data->output_file) {
                    fprintf(data->output_file, "%s\n", url);
                    fflush(data->output_file);
                }
                pthread_mutex_unlock(&data->output_mutex);

                if (data->current_level < data->max_level) {
                    FILE *wordlist = fopen(data->wordlist_path, "r");
                    if (wordlist) {
                        char line[MAX_LINE_LENGTH];
                        char new_url[MAX_URL_LENGTH];
                        while (fgets(line, sizeof(line), wordlist)) {
                            line[strcspn(line, "\n")] = 0;
                            size_t url_len = strlen(url);
                            size_t line_len = strlen(line);
                            
                            // Vérifier si on a assez d'espace pour "url/line\0"
                            if (url_len + line_len + 2 <= MAX_URL_LENGTH) {
                                // Copier l'URL de base
                                strncpy(new_url, url, MAX_URL_LENGTH - 1);
                                new_url[MAX_URL_LENGTH - 1] = '\0';
                                
                                // Ajouter le séparateur '/'
                                size_t pos = url_len;
                                if (pos < MAX_URL_LENGTH - 1) {
                                    new_url[pos] = '/';
                                    pos++;
                                
                                    // Ajouter le chemin
                                    strncpy(new_url + pos, line, MAX_URL_LENGTH - pos - 1);
                                    new_url[MAX_URL_LENGTH - 1] = '\0';
                                    
                                    enqueue(url_queue, new_url);
                                }
                            }
                        }
                        fclose(wordlist);
                    }
                }
            }
        }

        free(chunk.memory);
    }

    curl_easy_cleanup(curl);
    
    pthread_mutex_lock(&active_threads_mutex);
    active_threads--;
    pthread_mutex_unlock(&active_threads_mutex);
    
    return NULL;
}

// Fonction principale de scan
void scan_url(const char *base_url, const char *wordlist_path, int current_level, int max_level, FILE *output_file) {
    pthread_t threads[MAX_THREADS];
    struct ThreadData thread_data;
    
    pthread_mutex_init(&thread_data.output_mutex, NULL);
    pthread_mutex_init(&thread_data.curl_mutex, NULL);
    
    thread_data.base_url = (char*)base_url;
    thread_data.wordlist_path = (char*)wordlist_path;
    thread_data.current_level = current_level;
    thread_data.max_level = max_level;
    thread_data.output_file = output_file;

    url_queue = createQueue(1000);
    if (!url_queue) {
        printf("Erreur: Impossible de créer la file d'attente\n");
        return;
    }

    FILE *wordlist = fopen(wordlist_path, "r");
    if (wordlist) {
        char line[MAX_LINE_LENGTH];
        char url[MAX_URL_LENGTH];
        while (fgets(line, sizeof(line), wordlist)) {
            line[strcspn(line, "\n")] = 0;
            if (strlen(base_url) + strlen(line) + 2 < MAX_URL_LENGTH) {
                snprintf(url, sizeof(url), "%s/%s", base_url, line);
                enqueue(url_queue, url);
            }
        }
        fclose(wordlist);
    }

    active_threads = MAX_THREADS;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, scan_worker, &thread_data) != 0) {
            printf("Erreur: Impossible de créer le thread %d\n", i);
            active_threads--;
            continue;
        }
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < url_queue->capacity; i++) {
        free(url_queue->urls[i]);
    }
    free(url_queue->urls);
    free(url_queue);

    pthread_mutex_destroy(&thread_data.output_mutex);
    pthread_mutex_destroy(&thread_data.curl_mutex);
}

void print_help(const char *program_name) {
    printf("Usage: %s <url> <wordlist> [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -h           Affiche ce message d'aide\n");
    printf("  -n <niveau>  Définit la profondeur maximale du scan (défaut: 1)\n");
    printf("  -o <fichier> Spécifie le fichier de sortie pour les résultats\n\n");
    printf("Exemple:\n");
    printf("  %s http://exemple.com wordlist.txt -n 2 -o resultats.txt\n", program_name);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    if (argc < 4) {
        printf("Erreur: Arguments manquants\n");
        print_help(argv[0]);
        return 1;
    }

    char *url = argv[1];
    char *wordlist = argv[2];
    int max_level = 1;
    FILE *output_file = NULL;
    char *output_filename = NULL;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_level = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_filename = argv[i + 1];
            i++;
        }
    }

    if (output_filename) {
        output_file = fopen(output_filename, "w");
        if (!output_file) {
            printf("Erreur: Impossible de créer le fichier de sortie\n");
            return 1;
        }
        fprintf(output_file, "=== Résultats du scan ===\n");
        fprintf(output_file, "URL de base: %s\n", url);
        fprintf(output_file, "Nombre de niveaux: %d\n", max_level);
    }

    curl_global_init(CURL_GLOBAL_ALL);
    scan_url(url, wordlist, 1, max_level, output_file);
    curl_global_cleanup();

    if (output_file) {
        fclose(output_file);
    }

    return 0;
}