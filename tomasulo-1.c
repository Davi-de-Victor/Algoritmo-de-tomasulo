#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* CONSTANTES DE CONFIGURAÇÃO */
#define MAX_INSTRUCTIONS 100       // Número máximo de instruções no programa
#define MAX_REGISTERS 32           // Quantidade de registradores (R0-R31)
#define MAX_RESERVATION_STATIONS 12 // Total de estações de reserva
#define MAX_LOAD_BUFFERS 4         // Buffers para operações de LOAD
#define MAX_STORE_BUFFERS 4        // Buffers para operações de STORE

/* TIPOS DE OPERAÇÕES SUPORTADAS */
typedef enum {
    ADD,    // Soma
    SUB,    // Subtração
    MUL,    // Multiplicação
    DIV,    // Divisão
    LOAD,   // Carregamento de memória
    STORE,  // Armazenamento em memória
    NOP     // Operação nula (no-op)
} OperationType;

/* ESTRUTURA DE UMA INSTRUÇÃO */
typedef struct {
    OperationType op;  // Tipo da operação
    int dest;         // Registrador destino
    int src1;         // Registrador fonte 1
    int src2;         // Registrador fonte 2
    float immediate;  // Valor imediato (para LOAD/STORE)
    
    // Campos de temporização:
    int issued;       // Ciclo em que foi emitida
    int executed;     // Ciclo em que começou execução
    int written;      // Ciclo em que escreveu resultado
    int completed;    // Ciclo em que foi completada
} Instruction;

/* ESTRUTURA DE UMA ESTAÇÃO DE RESERVA */
typedef struct {
    int busy;           // 1 se ocupada, 0 se livre
    OperationType op;   // Tipo de operação
    float Vj, Vk;      // Valores dos operandos quando disponíveis
    int Qj, Qk;        // Tags das RS que estão produzindo os operandos
    int dest;          // Registrador destino
    int A;             // Campo auxiliar (endereço para LOAD/STORE)
    int time_remaining; // Ciclos restantes para término
} ReservationStation;

/* ESTADO DE UM REGISTRADOR */
typedef struct {
    float value;                // Valor atual do registrador
    int reservation_station;    // RS que está produzindo o valor (0 se pronto)
} RegisterStatus;

/* VARIÁVEIS GLOBAIS DO SIMULADOR */
RegisterStatus register_status[MAX_REGISTERS];  // Estado de todos os registradores
float registers[MAX_REGISTERS];                // Valores dos registradores

// Estações de reserva divididas por tipo:
ReservationStation add_rs[MAX_RESERVATION_STATIONS/2];    // Para ADD/SUB
ReservationStation mult_rs[MAX_RESERVATION_STATIONS/2];   // Para MUL/DIV
ReservationStation load_buffers[MAX_LOAD_BUFFERS];        // Para LOAD
ReservationStation store_buffers[MAX_STORE_BUFFERS];      // Para STORE

Instruction instructions[MAX_INSTRUCTIONS];  // Todas as instruções do programa
int instruction_count = 0;                  // Número de instruções carregadas
int current_cycle = 0;                      // Ciclo atual de simulação
int pc = 0;                                 // Contador de programa (próxima instrução)

/* FUNÇÃO AUXILIAR: CONVERTE OPERAÇÃO PARA STRING */
const char* op_to_str(OperationType op) {
    switch(op) {
        case ADD: return "ADD";
        case SUB: return "SUB";
        case MUL: return "MUL";
        case DIV: return "DIV";
        case LOAD: return "LOAD";
        case STORE: return "STORE";
        default: return "NOP";
    }
}

/* INICIALIZA O SIMULADOR */
void init_simulator() {
    // Zera todas as estruturas de dados
    memset(register_status, 0, sizeof(register_status));
    memset(registers, 0, sizeof(registers));
    memset(add_rs, 0, sizeof(add_rs));
    memset(mult_rs, 0, sizeof(mult_rs));
    memset(load_buffers, 0, sizeof(load_buffers));
    memset(store_buffers, 0, sizeof(store_buffers));
    
    instruction_count = 0;
    current_cycle = 0;
    pc = 0;
}

/* INTERPRETA UMA LINHA DE INSTRUÇÃO */
void parse_instruction(char* line) {
    if (instruction_count >= MAX_INSTRUCTIONS) return;
    
    Instruction instr = {NOP, -1, -1, -1, 0, 0, 0, 0, 0};
    char op[10];
    
    // Lê o mnemônico da operação
    if (sscanf(line, "%s", op) != 1) return;
    
    // Interpreta cada tipo de instrução
    if (strcasecmp(op, "ADD") == 0) {
        instr.op = ADD;
        sscanf(line, "%*s R%d R%d R%d", &instr.dest, &instr.src1, &instr.src2);
    } 
    // ... (casos para SUB, MUL, DIV)
    else if (strcasecmp(op, "LOAD") == 0) {
        instr.op = LOAD;
        sscanf(line, "%*s R%d %f(R%d)", &instr.dest, &instr.immediate, &instr.src1);
    } 
    else if (strcasecmp(op, "STORE") == 0) {
        instr.op = STORE;
        sscanf(line, "%*s R%d %f(R%d)", &instr.src1, &instr.immediate, &instr.src2);
    } 
    else {
        return; // Instrução inválida
    }
    
    instructions[instruction_count++] = instr;
}

/* CARREGA INSTRUÇÕES DE UM ARQUIVO */
void load_instructions(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Erro ao abrir arquivo");
        exit(1);
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file) {
        // Ignora linhas vazias e comentários
        if (line[0] == '\n' || line[0] == '#') continue;
        parse_instruction(line);
    }
    
    fclose(file);
}

/* EMITE UMA INSTRUÇÃO PARA UMA ESTAÇÃO DE RESERVA */
int issue_instruction(Instruction* instr) {
    ReservationStation* rs = NULL;
    int max_rs = 0;
    
    // Seleciona o tipo apropriado de RS
    switch(instr->op) {
        case ADD: case SUB:
            rs = add_rs;
            max_rs = MAX_RESERVATION_STATIONS/2;
            break;
        case MUL: case DIV:
            rs = mult_rs;
            max_rs = MAX_RESERVATION_STATIONS/2;
            break;
        case LOAD:
            rs = load_buffers;
            max_rs = MAX_LOAD_BUFFERS;
            break;
        case STORE:
            rs = store_buffers;
            max_rs = MAX_STORE_BUFFERS;
            break;
        default:
            return 0; // Tipo não suportado
    }
    
    // Procura uma RS livre
    for (int i = 0; i < max_rs; i++) {
        if (!rs[i].busy) {
            rs[i].busy = 1;
            rs[i].op = instr->op;
            rs[i].dest = instr->dest;
            
            // Trata operandos
            if (instr->op != STORE) {
                if (register_status[instr->src1].reservation_station == 0) {
                    rs[i].Vj = registers[instr->src1]; // Operando pronto
                    rs[i].Qj = 0;
                } else {
                    rs[i].Qj = register_status[instr->src1].reservation_station; // Aguarda RS
                }
            }
            
            // Operações aritméticas usam dois operandos
            if (instr->op == ADD || instr->op == SUB || 
                instr->op == MUL || instr->op == DIV) {
                if (register_status[instr->src2].reservation_station == 0) {
                    rs[i].Vk = registers[instr->src2];
                    rs[i].Qk = 0;
                } else {
                    rs[i].Qk = register_status[instr->src2].reservation_station;
                }
            }
            
            // LOAD/STORE usam endereço calculado
            if (instr->op == LOAD || instr->op == STORE) {
                rs[i].A = instr->immediate;
                if (instr->op == STORE) {
                    if (register_status[instr->src1].reservation_station == 0) {
                        rs[i].Vj = registers[instr->src1];
                        rs[i].Qj = 0;
                    } else {
                        rs[i].Qj = register_status[instr->src1].reservation_station;
                    }
                }
            }
            
            // Define latência conforme operação
            switch(instr->op) {
                case ADD: case SUB: rs[i].time_remaining = 2; break;
                case MUL: rs[i].time_remaining = 10; break;
                case DIV: rs[i].time_remaining = 40; break;
                case LOAD: case STORE: rs[i].time_remaining = 2; break;
                default: rs[i].time_remaining = 0;
            }
            
            // Atualiza estado do registrador destino
            if (instr->op != STORE) {
                register_status[instr->dest].reservation_station = 
                    (rs == add_rs) ? i+1 : 
                    (rs == mult_rs) ? i+1+MAX_RESERVATION_STATIONS/2 :
                    (rs == load_buffers) ? i+1+MAX_RESERVATION_STATIONS : 
                    i+1+MAX_RESERVATION_STATIONS+MAX_LOAD_BUFFERS;
            }
            
            instr->issued = current_cycle;
            return 1; // Emissão bem sucedida
        }
    }
    
    return 0; // Nenhuma RS livre
}

/* EXECUTA UM CICLO NAS OPERAÇÕES */
void execute_operations() {
    // Processa estações de ADD/SUB
    for (int i = 0; i < MAX_RESERVATION_STATIONS/2; i++) {
        if (add_rs[i].busy && !add_rs[i].Qj && !add_rs[i].Qk && add_rs[i].time_remaining > 0) {
            add_rs[i].time_remaining--;
            if (add_rs[i].time_remaining == 0) {
                instructions[add_rs[i].dest].executed = current_cycle;
            }
        }
    }
    
    // Processa estações de MUL/DIV
    for (int i = 0; i < MAX_RESERVATION_STATIONS/2; i++) {
        if (mult_rs[i].busy && !mult_rs[i].Qj && !mult_rs[i].Qk && mult_rs[i].time_remaining > 0) {
            mult_rs[i].time_remaining--;
            if (mult_rs[i].time_remaining == 0) {
                instructions[mult_rs[i].dest].executed = current_cycle;
            }
        }
    }
    
    // Processa buffers de LOAD
    for (int i = 0; i < MAX_LOAD_BUFFERS; i++) {
        if (load_buffers[i].busy && !load_buffers[i].Qj && load_buffers[i].time_remaining > 0) {
            load_buffers[i].time_remaining--;
            if (load_buffers[i].time_remaining == 0) {
                instructions[load_buffers[i].dest].executed = current_cycle;
            }
        }
    }
    
    // Processa buffers de STORE
    for (int i = 0; i < MAX_STORE_BUFFERS; i++) {
        if (store_buffers[i].busy && !store_buffers[i].Qj && store_buffers[i].time_remaining > 0) {
            store_buffers[i].time_remaining--;
            if (store_buffers[i].time_remaining == 0) {
                instructions[store_buffers[i].dest].executed = current_cycle;
            }
        }
    }
}

/* ESCREVE RESULTADOS PRONTOS NO CDB */
void writeback_results() {
    // Processa estações de ADD/SUB
    for (int i = 0; i < MAX_RESERVATION_STATIONS/2; i++) {
        if (add_rs[i].busy && add_rs[i].time_remaining == 0) {
            float result = 0;
            switch(add_rs[i].op) {
                case ADD: result = add_rs[i].Vj + add_rs[i].Vk; break;
                case SUB: result = add_rs[i].Vj - add_rs[i].Vk; break;
            }
            
            // Atualiza registrador destino
            registers[add_rs[i].dest] = result;
            register_status[add_rs[i].dest].reservation_station = 0;
            instructions[add_rs[i].dest].written = current_cycle;
            
            // Broadcast no CDB - atualiza dependências
            for (int j = 0; j < MAX_RESERVATION_STATIONS/2; j++) {
                if (add_rs[j].Qj == i+1) { add_rs[j].Vj = result; add_rs[j].Qj = 0; }
                if (add_rs[j].Qk == i+1) { add_rs[j].Vk = result; add_rs[j].Qk = 0; }
                if (mult_rs[j].Qj == i+1) { mult_rs[j].Vj = result; mult_rs[j].Qj = 0; }
                if (mult_rs[j].Qk == i+1) { mult_rs[j].Vk = result; mult_rs[j].Qk = 0; }
            }
            
            // Atualiza buffers de LOAD/STORE
            for (int j = 0; j < MAX_LOAD_BUFFERS; j++) {
                if (load_buffers[j].Qj == i+1) { load_buffers[j].Vj = result; load_buffers[j].Qj = 0; }
                if (load_buffers[j].Qk == i+1) { load_buffers[j].Vk = result; load_buffers[j].Qk = 0; }
            }
            
            for (int j = 0; j < MAX_STORE_BUFFERS; j++) {
                if (store_buffers[j].Qj == i+1) { store_buffers[j].Vj = result; store_buffers[j].Qj = 0; }
                if (store_buffers[j].Qk == i+1) { store_buffers[j].Vk = result; store_buffers[j].Qk = 0; }
            }
            
            add_rs[i].busy = 0;
            instructions[add_rs[i].dest].completed = current_cycle;
        }
    }
    
    // Processa estações de MUL/DIV (similar ao ADD/SUB)
    // ...
    
    // Processa buffers de LOAD
    for (int i = 0; i < MAX_LOAD_BUFFERS; i++) {
        if (load_buffers[i].busy && load_buffers[i].time_remaining == 0) {
            float result = 1.0f; // Simula valor carregado
            
            registers[load_buffers[i].dest] = result;
            register_status[load_buffers[i].dest].reservation_station = 0;
            instructions[load_buffers[i].dest].written = current_cycle;
            
            // Broadcast no CDB
            for (int j = 0; j < MAX_RESERVATION_STATIONS/2; j++) {
                if (add_rs[j].Qj == i+1+MAX_RESERVATION_STATIONS) { 
                    add_rs[j].Vj = result; add_rs[j].Qj = 0; 
                }
                // ... (atualiza outras dependências)
            }
            
            load_buffers[i].busy = 0;
            instructions[load_buffers[i].dest].completed = current_cycle;
        }
    }
    
    // Processa buffers de STORE
    for (int i = 0; i < MAX_STORE_BUFFERS; i++) {
        if (store_buffers[i].busy && store_buffers[i].time_remaining == 0) {
            store_buffers[i].busy = 0;
            instructions[store_buffers[i].dest].completed = current_cycle;
        }
    }
}

/* MOSTRA O ESTADO ATUAL DO SIMULADOR */
void print_status() {
    printf("\n=== Ciclo %d ===\n", current_cycle);
    
    // Mostra registradores
    printf("\nRegistradores:\n");
    for (int i = 0; i < MAX_REGISTERS; i++) {
        printf("R%d: %.2f", i, registers[i]);
        if (register_status[i].reservation_station != 0) {
            printf(" [RS%d]", register_status[i].reservation_station);
        }
        printf("\n");
    }
    
    // Mostra estações de reserva ocupadas
    printf("\nEstações de Reserva (ADD/SUB):\n");
    for (int i = 0; i < MAX_RESERVATION_STATIONS/2; i++) {
        if (add_rs[i].busy) {
            printf("RS%d: %s Vj=%.2f Vk=%.2f Qj=%d Qk=%d Dest=R%d Time=%d\n",
                   i+1, op_to_str(add_rs[i].op), add_rs[i].Vj, add_rs[i].Vk,
                   add_rs[i].Qj, add_rs[i].Qk, add_rs[i].dest, add_rs[i].time_remaining);
        }
    }
    
    // ... (mostra outras estações de reserva)
    
    // Mostra progresso das instruções
    printf("\nInstruções:\n");
    for (int i = 0; i < instruction_count; i++) {
        printf("%d: %s R%d R%d R%d", i, op_to_str(instructions[i].op), 
               instructions[i].dest, instructions[i].src1, instructions[i].src2);
        if (instructions[i].issued) printf(" [Issue@%d]", instructions[i].issued);
        if (instructions[i].executed) printf(" [Exec@%d]", instructions[i].executed);
        if (instructions[i].written) printf(" [Write@%d]", instructions[i].written);
        if (instructions[i].completed) printf(" [Complete@%d]", instructions[i].completed);
        printf("\n");
    }
}

/* EXECUTA UM CICLO COMPLETO DO SIMULADOR */
void run_cycle() {
    // 1. Emite nova instrução se possível
    if (pc < instruction_count) {
        if (issue_instruction(&instructions[pc])) {
            pc++;
        }
    }
    
    // 2. Executa operações nas estações de reserva
    execute_operations();
    
    // 3. Escreve resultados prontos
    writeback_results();
    
    current_cycle++;
}

/* FUNÇÃO PRINCIPAL */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Uso: %s <arquivo_instrucoes.txt>\n", argv[0]);
        return 1;
    }
    
    init_simulator();
    load_instructions(argv[1]);
    
    printf("Simulador de Tomasulo - Pressione Enter para avançar um ciclo, 'q' para sair\n");
    
    // Loop principal de simulação
    while (1) {
        print_status();
        
        char input = getchar();
        if (input == 'q') break;
        
        run_cycle();
    }
    
    return 0;
}