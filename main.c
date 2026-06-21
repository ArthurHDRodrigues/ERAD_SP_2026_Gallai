#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

// Função auxiliar para escrever o caminho em um buffer de string na memória
// Retorna a quantidade de caracteres escritos
int sprint_path_yaml(char* buffer, int* path, int len) {
    int pos = 0;
    pos += sprintf(buffer + pos, "\"");
    for (int i = 0; i < len; i++) {
        pos += sprintf(buffer + pos, "%d%s", path[i], (i < len - 1) ? " -> " : "");
    }
    pos += sprintf(buffer + pos, "\"\n");
    return pos;
}

void dfs_longest_path(int u, int** adj, int* visited, int* current_path, int current_len, 
                      int* max_local, int* max_len, int n, int skip_vertex, int target_len) {
    if (*max_len == target_len) return;

    visited[u] = 1;
    current_path[current_len] = u;
    current_len++;

    int has_unvisited_neighbor = 0;

    for (int i = 0; i < 3; i++) {
        int v = adj[u][i];
        if (v == -1) continue;
        if (v == skip_vertex) continue;

        if (!visited[v]) {
            has_unvisited_neighbor = 1;
            dfs_longest_path(v, adj, visited, current_path, current_len, max_local, max_len, n, skip_vertex, target_len);
        }
    }

    if (!has_unvisited_neighbor) {
        if (current_len > *max_len) {
            *max_len = current_len;
            memcpy(max_local, current_path, current_len * sizeof(int));
        }
    }

    visited[u] = 0;
}

int* find_longest_path(int** adj, int n, int skip_vertex, int* out_len) {
    if (n <= 0) {
        *out_len = 0;
        return NULL;
    }

    int* max_local = (int*)malloc(n * sizeof(int));
    int* current_path = (int*)malloc(n * sizeof(int));
    int* visited = (int*)calloc(n, sizeof(int));
    
    int max_len = 0;
    int target_len = (skip_vertex == -1) ? n : n - 1;

    for (int i = 0; i < n; i++) {
        if (i == skip_vertex) continue;
        dfs_longest_path(i, adj, visited, current_path, 0, max_local, &max_len, n, skip_vertex, target_len);
        if (max_len == target_len) break;
    }

    free(current_path);
    free(visited);

    *out_len = max_len;
    return max_local;
}

int** parse_graph6_3regular(const char* g6_str, int* n_out) {
    int n = 0, char_idx = 0;

    if (g6_str[0] == 126) {
        n = ((g6_str[1] - 63) << 12) | ((g6_str[2] - 63) << 6) | (g6_str[3] - 63);
        char_idx = 4;
    } else {
        n = g6_str[0] - 63;
        char_idx = 1;
    }

    *n_out = n;
    int** adj = (int**)malloc(n * sizeof(int*));
    int* current_degree = (int*)calloc(n, sizeof(int));

    for (int i = 0; i < n; i++) {
        adj[i] = (int*)malloc(3 * sizeof(int));
        for(int k=0; k<3; k++) adj[i][k] = -1;
    }

    int bit_idx = 5;
    int current_char_val = g6_str[char_idx] - 63;

    for (int i = 1; i < n; i++) {
        for (int j = 0; j < i; j++) {
            int bit = (current_char_val >> bit_idx) & 1;

            if (bit == 1) {
                if (current_degree[i] < 3) adj[i][current_degree[i]++] = j;
                if (current_degree[j] < 3) adj[j][current_degree[j]++] = i;
            }

            bit_idx--;
            if (bit_idx < 0) {
                bit_idx = 5;
                char_idx++;
                if (g6_str[char_idx] != '\0') {
                    current_char_val = g6_str[char_idx] - 63;
                } else current_char_val = 0;
            }
        }
    }
    free(current_degree);
    return adj;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Uso: %s <arquivo_de_entrada.g6>\n", argv[0]);
        return 1;
    }

    const char* input_filename = argv[1];
    FILE* file = fopen(input_filename, "r");
    if (!file) {
        perror("Erro ao abrir o arquivo de entrada");
        return 1;
    }

    // Geração do nome do arquivo YAML
    char yaml_filename[256];
    strncpy(yaml_filename, input_filename, sizeof(yaml_filename)-1);
    yaml_filename[255] = '\0';
    char *ext = strrchr(yaml_filename, '.');
    if (ext) *ext = '\0';
    strncat(yaml_filename, ".yaml", sizeof(yaml_filename) - strlen(yaml_filename) - 1);

    // =========================================================================
    // ETAPA 1: Leitura de todas as linhas para a memória (Sequencial)
    // =========================================================================
    int capacity = 1000;
    char** lines = (char**)malloc(capacity * sizeof(char*));
    int num_lines = 0;
    char file_buffer[100000];

    while (fgets(file_buffer, sizeof(file_buffer), file)) {
        file_buffer[strcspn(file_buffer, "\r\n")] = '\0';
        if (strlen(file_buffer) == 0) continue;

        if (num_lines >= capacity) {
            capacity *= 2;
            lines = (char**)realloc(lines, capacity * sizeof(char*));
        }
        lines[num_lines] = strdup(file_buffer);
        num_lines++;
    }
    fclose(file);

    // Array para guardar os blocos YAML de cada grafo na mesma ordem do arquivo original
    char** yaml_results = (char**)calloc(num_lines, sizeof(char*));

    printf("Iniciando processamento paralelo de %d grafos...\n", num_lines);

    // =========================================================================
    // ETAPA 2: Processamento Paralelo (OpenMP Nível 1)
    // Usamos schedule(dynamic) porque grafos diferentes levam tempos muito 
    // diferentes para calcular a busca DFS (NP-Difícil).
    // =========================================================================
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < num_lines; i++) {
        // TODAS as variáveis a partir daqui devem ser declaradas localmente 
        // para garantir que sejam privadas a cada thread e evitar Race Conditions.
        int n = 0;
        int** adj = parse_graph6_3regular(lines[i], &n);

        int p = 0;
        int* P = find_longest_path(adj, n, -1, &p);

        // Buffer local generoso (1MB) para montar a string YAML deste grafo específico
        char local_yaml_buf[1048576]; 
        int pos = 0;

        pos += sprintf(local_yaml_buf + pos, "- graph6: \"%s\"\n  certificate:\n", lines[i]);

        if (p == n) {
            pos += sprintf(local_yaml_buf + pos, "    type: hamiltonian\n    proof: ");
            pos += sprint_path_yaml(local_yaml_buf + pos, P, p);
        } else {
            int is_gallai = 0;
            int gallai_v = -1;
            int** alt_paths = (int**)calloc(p, sizeof(int*)); // Tratamento de tcache mantido
            int* alt_lens = (int*)malloc(p * sizeof(int));

            for (int k = 0; k < p; k++) {
                int v = P[k];
                int p_prime = 0;
                int* P_prime = find_longest_path(adj, n, v, &p_prime);

                if (p_prime < p) {
                    is_gallai = 1;
                    gallai_v = v;
                    free(P_prime);
                    break; 
                } else {
                    alt_paths[k] = P_prime;
                    alt_lens[k] = p_prime;
                }
            }

            if (is_gallai) {
                pos += sprintf(local_yaml_buf + pos, "    type: gallai\n    proof:\n");
                pos += sprintf(local_yaml_buf + pos, "      vertex: %d\n", gallai_v);
                pos += sprintf(local_yaml_buf + pos, "      longest_path_length: %d\n", p);
                pos += sprintf(local_yaml_buf + pos, "      base_longest_path: ");
                pos += sprint_path_yaml(local_yaml_buf + pos, P, p);
                
                for(int k = 0; k < p; k++) {
                    if (alt_paths[k]) free(alt_paths[k]);
                }
            } else {
                pos += sprintf(local_yaml_buf + pos, "    type: nogallai\n    proof:\n");
                pos += sprintf(local_yaml_buf + pos, "      base_longest_path: ");
                pos += sprint_path_yaml(local_yaml_buf + pos, P, p);
                pos += sprintf(local_yaml_buf + pos, "      alternative_paths:\n");
                
                for (int k = 0; k < p; k++) {
                    pos += sprintf(local_yaml_buf + pos, "        %d: ", P[k]);
                    pos += sprint_path_yaml(local_yaml_buf + pos, alt_paths[k], alt_lens[k]);
                    free(alt_paths[k]);
                }
            }
            free(alt_paths);
            free(alt_lens);
        }

        // Salva a string formatada no array de resultados usando strdup para alocar o tamanho exato
        yaml_results[i] = strdup(local_yaml_buf);

        // Limpeza de memória do grafo iterado pela Thread
        free(P);
        for (int j = 0; j < n; j++) free(adj[j]);
        free(adj);
    }

    // =========================================================================
    // ETAPA 3: Escrita e Limpeza (Sequencial)
    // =========================================================================
    FILE* yaml_out = fopen(yaml_filename, "w");
    if (!yaml_out) {
        perror("Erro ao criar o arquivo YAML final");
        return 1;
    }

    for (int i = 0; i < num_lines; i++) {
        fprintf(yaml_out, "%s", yaml_results[i]);
        free(lines[i]);        // Libera a string G6 original
        free(yaml_results[i]); // Libera a string YAML resultante
    }

    free(lines);
    free(yaml_results);
    fclose(yaml_out);

    printf("Concluido! Relatorio gerado com sucesso: %s\n", yaml_filename);
    return 0;
}
