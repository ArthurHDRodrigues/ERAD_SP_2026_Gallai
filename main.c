#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

// Função auxiliar para escrever o caminho no YAML (Inalterada)
void print_path_yaml(FILE* out, int* path, int len) {
    fprintf(out, "\"");
    for (int i = 0; i < len; i++) {
        fprintf(out, "%d%s", path[i], (i < len - 1) ? " -> " : "");
    }
    fprintf(out, "\"\n");
}

// A recursão pura não sofre alterações, o isolamento ocorre na função chamadora
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

// PARALELISMO 2.A: Busca do caminho base
int* find_longest_path(int** adj, int n, int skip_vertex, int* out_len) {
    if (n <= 0) {
        *out_len = 0;
        return NULL;
    }

    int global_max_len = 0;
    int* global_max_path = (int*)malloc(n * sizeof(int));
    int target_len = (skip_vertex == -1) ? n : n - 1;
    
    // Flag volátil para comunicação entre threads. Se 1, todas abortam a busca.
    volatile int found_target = 0; 

    // Cria a região paralela
    #pragma omp parallel
    {
        // Memória Thread-Local: Cada thread precisa das suas próprias variáveis de estado
        int* local_max_path = (int*)malloc(n * sizeof(int));
        int* current_path = (int*)malloc(n * sizeof(int));
        int* visited = (int*)calloc(n, sizeof(int));
        int local_max_len = 0;

        // Divide os vértices raiz da busca entre as threads
        #pragma omp for schedule(dynamic)
        for (int i = 0; i < n; i++) {
            if (found_target) continue; // Early exit check simulando um "break"
            if (i == skip_vertex) continue;

            dfs_longest_path(i, adj, visited, current_path, 0, local_max_path, &local_max_len, n, skip_vertex, target_len);

            if (local_max_len == target_len) {
                found_target = 1; // Avisa as outras threads
            }
        }

        // Seção crítica: apenas uma thread atualiza o recorde global por vez
        #pragma omp critical
        {
            if (local_max_len > global_max_len) {
                global_max_len = local_max_len;
                memcpy(global_max_path, local_max_path, local_max_len * sizeof(int));
            }
        }

        // Limpeza Thread-Local
        free(local_max_path);
        free(current_path);
        free(visited);
    }

    *out_len = global_max_len;
    return global_max_path;
}

// Parse g6 (Inalterado)
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

    char yaml_filename[256];
    strncpy(yaml_filename, input_filename, sizeof(yaml_filename)-1);
    yaml_filename[255] = '\0';
    char *ext = strrchr(yaml_filename, '.');
    if (ext) *ext = '\0';
    strncat(yaml_filename, ".yaml", sizeof(yaml_filename) - strlen(yaml_filename) - 1);

    FILE* yaml_out = fopen(yaml_filename, "w");
    if (!yaml_out) {
        perror("Erro ao criar o arquivo YAML");
        fclose(file);
        return 1;
    }

    char buffer[100000];
    
    // Processamento sequencial do arquivo (Linha a Linha)
    while (fgets(buffer, sizeof(buffer), file)) {
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        int n = 0;
        int** adj = parse_graph6_3regular(buffer, &n);

        // a) Encontra o caminho base de forma paralela (Nível 2.A aplicado dentro da função)
        int p = 0;
        int* P = find_longest_path(adj, n, -1, &p);

        fprintf(yaml_out, "- graph6: \"%s\"\n", buffer);
        fprintf(yaml_out, "  certificate:\n");

        if (p == n) {
            fprintf(yaml_out, "    type: hamiltonian\n");
            fprintf(yaml_out, "    proof: ");
            print_path_yaml(yaml_out, P, p);
        } else {
            // Flags voláteis compartilhadas para interrupção do loop
            volatile int is_gallai = 0;
            int gallai_v = -1;
            
            int** alt_paths = (int**)calloc(p, sizeof(int*));
            int* alt_lens = (int*)malloc(p * sizeof(int));

            // PARALELISMO 2.B: Verificação dos vértices de Gallai
            // Nota: Por padrão o OpenMP não aninha paralelismo (nested parallelism). 
            // Portanto, a chamada de 'find_longest_path' aqui dentro rodará sequencialmente
            // para cada thread deste loop, distribuindo a carga perfeitamente sem explosão de threads.
            #pragma omp parallel for schedule(dynamic)
            for (int k = 0; k < p; k++) {
                if (is_gallai) continue; // Simula o "break" se outra thread já achou um Gallai

                int v = P[k];
                int p_prime = 0;
                int* P_prime = find_longest_path(adj, n, v, &p_prime);

                if (p_prime < p) {
                    // Proteção na escrita da flag de condição de parada
                    #pragma omp critical
                    {
                        if (!is_gallai) { // Double check pattern
                            is_gallai = 1;
                            gallai_v = v;
                        }
                    }
                    free(P_prime);
                } else {
                    alt_paths[k] = P_prime; // Escrita segura: cada thread altera um índice 'k' único
                    alt_lens[k] = p_prime;
                }
            }

            if (is_gallai) {
                fprintf(yaml_out, "    type: gallai\n");
                fprintf(yaml_out, "    proof:\n");
                fprintf(yaml_out, "      vertex: %d\n", gallai_v);
                fprintf(yaml_out, "      longest_path_length: %d\n", p);
                fprintf(yaml_out, "      base_longest_path: ");
                print_path_yaml(yaml_out, P, p);
                
                for(int k = 0; k < p; k++) {
                    if (alt_paths[k]) free(alt_paths[k]);
                }
            } else {
                fprintf(yaml_out, "    type: nogallai\n");
                fprintf(yaml_out, "    proof:\n");
                fprintf(yaml_out, "      base_longest_path: ");
                print_path_yaml(yaml_out, P, p);
                fprintf(yaml_out, "      alternative_paths:\n");
                
                for (int k = 0; k < p; k++) {
                    fprintf(yaml_out, "        %d: ", P[k]);
                    print_path_yaml(yaml_out, alt_paths[k], alt_lens[k]);
                    free(alt_paths[k]);
                }
            }
            free(alt_paths);
            free(alt_lens);
        }

        free(P);
        for (int i = 0; i < n; i++) free(adj[i]);
        free(adj);
    }

    printf("Relatorio gerado com sucesso: %s\n", yaml_filename);

    fclose(file);
    fclose(yaml_out);
    return 0;
}
