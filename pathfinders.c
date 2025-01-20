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
    pthread_mutex_t *output_mutex;
    pthread_mutex_t *curl_mutex;
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

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL) {
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Initialisation de la file d'attente
struct Queue* createQueue(int capacity) {
    struct Queue *queue = (struct Queue*)malloc(sizeof(struct Queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->urls = (char**)malloc(capacity * sizeof(char*));
    for (int i = 0; i < capacity; i++) {
        queue->urls[i] = (char*)malloc(MAX_URL_LENGTH * sizeof(char));
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
    
    strncpy(url, queue->urls[queue->front], MAX_URL_LENGTH);
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

        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
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
                pthread_mutex_lock(data->output_mutex);
                printf("[TROUVÉ] %s\n", url);
                if (data->output_file) {
                    fprintf(data->output_file, "%s\n", url);
                }
                pthread_mutex_unlock(data->output_mutex);

                // Ajouter les nouvelles URLs pour le niveau suivant
                if (data->current_level < data->max_level) {
                    FILE *wordlist = fopen(data->wordlist_path, "r");
                    if (wordlist) {
                        char line[MAX_LINE_LENGTH];
                        char new_url[MAX_URL_LENGTH];
                        while (fgets(line, sizeof(line), wordlist)) {
                            line[strcspn(line, "\n")] = 0;
                            snprintf(new_url, sizeof(new_url), "%s/%s", url, line);
                            enqueue(url_queue, new_url);
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
    pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t curl_mutex = PTHREAD_MUTEX_INITIALIZER;
    struct ThreadData thread_data = {
        .base_url = (char*)base_url,
        .wordlist_path = (char*)wordlist_path,
        .current_level = current_level,
        .max_level = max_level,
        .output_file = output_file,
        .output_mutex = &output_mutex,
        .curl_mutex = &curl_mutex
    };

    // Initialiser la file d'attente
    url_queue = createQueue(1000);

    // Remplir la file d'attente avec les URLs initiales
    FILE *wordlist = fopen(wordlist_path, "r");
    if (wordlist) {
        char line[MAX_LINE_LENGTH];
        char url[MAX_URL_LENGTH];
        while (fgets(line, sizeof(line), wordlist)) {
            line[strcspn(line, "\n")] = 0;
            snprintf(url, sizeof(url), "%s/%s", base_url, line);
            enqueue(url_queue, url);
        }
        fclose(wordlist);
    }

    // Créer les threads
    active_threads = MAX_THREADS;
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_create(&threads[i], NULL, scan_worker, &thread_data);
    }

    // Attendre que tous les threads terminent
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Nettoyer la file d'attente
    for (int i = 0; i < url_queue->capacity; i++) {
        free(url_queue->urls[i]);
    }
    free(url_queue->urls);
    free(url_queue);
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

    // Vérifier si l'aide est demandée
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    // Vérifier les arguments minimums requis
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