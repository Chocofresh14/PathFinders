#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define MAX_URL_LENGTH 2048
#define MAX_LINE_LENGTH 256

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

void scan_url(const char *base_url, const char *wordlist_path, int current_level, int max_level, FILE *output_file) {
    if (current_level > max_level) return;

    CURL *curl;
    CURLcode res;
    FILE *wordlist;
    char line[MAX_LINE_LENGTH];
    char url[MAX_URL_LENGTH];
    struct MemoryStruct chunk;

    wordlist = fopen(wordlist_path, "r");
    if (!wordlist) {
        printf("Erreur: Impossible d'ouvrir la wordlist\n");
        return;
    }

    curl = curl_easy_init();
    if (!curl) {
        printf("Erreur: Initialisation CURL échouée\n");
        fclose(wordlist);
        return;
    }

    // Si un fichier de sortie est spécifié, écrire l'en-tête du niveau
    if (output_file) {
        fprintf(output_file, "\n=== Niveau %d ===\n", current_level);
    }

    while (fgets(line, sizeof(line), wordlist)) {
        line[strcspn(line, "\n")] = 0;  // Remove newline

        snprintf(url, sizeof(url), "%s/%s", base_url, line);

        chunk.memory = malloc(1);
        chunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200) {
                printf("[TROUVÉ] %s\n", url);
                if (output_file) {
                    fprintf(output_file, "%s\n", url);
                }
                if (current_level < max_level) {
                    scan_url(url, wordlist_path, current_level + 1, max_level, output_file);
                }
            }
        }

        free(chunk.memory);
    }

    curl_easy_cleanup(curl);
    fclose(wordlist);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <url> <wordlist> -n <niveau> [-o <output_file>]\n", argv[0]);
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