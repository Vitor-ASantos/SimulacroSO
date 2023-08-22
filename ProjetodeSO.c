#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <pthread.h>
#include <termios.h>
#include <semaphore.h>
  

#define MEMORY_SIZE 1250  // tamanho  total da memória em kbytes
#define BLOCK_SIZE 8 // tamanho do bloco
#define SLEEP 10000 // tempo de pausa para possibilitar uma visualização melhor
#define number_of_instructions 7

typedef struct commando{
  char command[6];
  int number;  //-1 se não tiver.
}Instrucao;

//Estrutura para armazenamento das variaveis de semaforos no codigo
typedef struct semaforo{
  char id;
  int value;
}Semaforos;

// será uma lista simplesmente encadeada.
// Armazena informações sobre o processo que serão utilizadas pelo escalonador
typedef struct bcp {
  int process_id; // identificador do processo
  char process_name[25];//nome do processo
  int segment_id; //local das informaçoes do processo namemoria
  int segment_size; //Tamanho do segmento do processo em kbytes
  int PC;// Program Counter indica a instrução atual a ser executada do programa
  int process_state; // [1 - ready , -1 blocked, -2 bloqueio de semaforo(Waiting),0 runing , 2 - finish]
  int remaining_time; // Tempo restante necessario para execução do prcesso 
  int process_priority; // Valor arbritario que define a prioridade do processo
  char semaphores[10][2]; // Variaveis de semaforos no programa
  struct bcp *next, *prev;
} BCP;

// Armazenará as instruções dos comandos do processo
// será armazenado o endereço da lista de comandos que aponta para lista de comandos desse processo
// O correto seria armazenar diretamente na memória porem não seria possivel armazenar uma lista ligada
// numa posição especifica da memória. E a maneira que pensamos inicialmente seria permitido armazenamento
// de conteudos que não fossem necessariaente do tipo Commands, por isso não copiamos diretamente
typedef struct block {
  int state; // se for -1, então está livre.
  Instrucao page[number_of_instructions]; // instruções na memorias
} Block;


//Um vetor de blocos que compõe a memória do SO
typedef struct memory {
  Block blocks[MEMORY_SIZE/BLOCK_SIZE];  // blocos com 8kb.
  int current_occupation;  // Quantas paginas estão ocupadas na memória   
} Memory;


//Estruct que armazenará o processo de uso privativo de E/S no SO
typedef struct fila{
    int Process_id; // Identificação do processo que enviou a instrução
    Instrucao tarefa; // a tarefa a ser executada
    struct fila *next; // Proximo elemento da lista
}Fila_IO;


//Definição de variaveis que serão utilizadas em todo o SO
Memory memory;
BCP *bcp_head = NULL ,*bcp_tail = NULL;
int idProc = 0;
Fila_IO *Fila_Impressao = NULL,*Fila_disc=NULL;
Semaforos Semaforos_global[30];
sem_t semaphore,semaphore_2,semaphore_3,semaphore_4,semaphore_5;

//Incializa As variaveis
void reset() {
  int i;
  memory.current_occupation = 0;
  for (i = 0; i < (MEMORY_SIZE/BLOCK_SIZE); i++) {
    memory.blocks[i].state = -1;
  }

  for(int i =0; i<30; i++){
    Semaforos_global[i].id = 0;
    Semaforos_global[i].value = 1;
 }

}


//////////////////// FUNCOES DO SO //////////////////////////////////


//Recebe o nome do Processo 
//Retorna nada
//Lê o arquivo do processo salva o primeiro bloco do arquivo de texto no BCP
//Pois são informações relacionadas a informações sobre o processo
//Armazena as intruções numa lista ligada do tipo Commands que será armazenada na memória
void processCreate(char file_name[100]) {
  FILE *fp = fopen(file_name, "r");
  if(fp == NULL){
    printf("Processo inexistente \n");
    return;
  }
  BCP *new = malloc(sizeof(BCP));
  int number_pages;
  int k =0;
  int i=0;
  int ver=0;
  new->remaining_time = 0;  

  //inicializa os semaforos do programa
  char semaphor_aux[22];
  for(int i=0;i<10;i++){
    new->semaphores[i][0]= 0;
  }
  new->process_id = idProc;
  idProc++;
  fscanf(fp, "%s\n", new->process_name);
  fscanf(fp, "%d\n", &new->segment_id);
  fscanf(fp, "%d\n", &new->process_priority);
  fscanf(fp, "%d\n", &new->segment_size);
  fscanf(fp, "%[^\n]", semaphor_aux);
  
  i=0;

  //Le a linha relacionada aos semaforos do programa com espaços
  //e armazazena nas variaveis semaforos do processo
  while(semaphor_aux[k]!=0 || semaphor_aux[k-1]!=0){
     new->semaphores[i][0] = semaphor_aux[k];
     i++;
     k+=2;
  }
  i=0;
  k=0;

  //verifica se um novo semaforo foi criado e em caso afirmativo armazena nos semaforos globais
  while(new->semaphores[i][0]!= 0){
    k=0;
    ver=0;
     while(Semaforos_global[k].id!=0){
       if(new->semaphores[i][0]==Semaforos_global[k].id)
           ver = 1;  
       k++;  
     }
     if(ver==0){
      k=0;
       while(Semaforos_global[k].id!=0){
         k++;
       }
       Semaforos_global[k].id = new->semaphores[i][0];
       Semaforos_global[k].value = 1;
     }
     i++;
  }
  
  //Memory load:

  number_pages = ceil(new->segment_size / BLOCK_SIZE);
  int pg_empty = 0;
  int position = -1 ;
  i = 0;

  //Procura por numero de paginas livres consecutivas pra armazenar o segmento do processo na memória
  while( position ==-1 && i < (MEMORY_SIZE/BLOCK_SIZE) ){
     if (memory.blocks[i].state == -1){
        pg_empty++;
     }
     else 
       pg_empty = 0 ;
     if(pg_empty==number_pages){
       position = i - pg_empty + 1;
       break;
     }
     i++;
  }
  if(position==-1){
    printf("ERRO - nao ha espaço na memoria\n") ;
    return;
  }

  //Armazenando o processo na memória
  else{
       new->segment_id = position;
       for (int i= new->segment_id; i<(new->segment_id + number_pages);i++){
          memory.blocks[i].state = 1; // coloca como bloqueado na memoria
       }
      i=0;
      int block_number = new->segment_id;
       while (!feof(fp)) {
        
       if(i>=number_of_instructions){
        block_number++;
        i=0;
       }
       fscanf(fp, "%s", memory.blocks[block_number].page[i].command);
        if (!strcmp(memory.blocks[block_number].page[i].command, "exec") ||
            !strcmp(memory.blocks[block_number].page[i].command, "read") ||
            !strcmp(memory.blocks[block_number].page[i].command, "write") ||
            !strcmp(memory.blocks[block_number].page[i].command, "print")) {
          fscanf(fp, "%d", &memory.blocks[block_number].page[i].number);
          new->remaining_time += memory.blocks[block_number].page[i].number;
        }
        else if(memory.blocks[block_number].page[i].command[1] == '('){
          memory.blocks[block_number].page[i].number = 200;
          new->remaining_time += 200;
        }
        if (!feof(fp)) {
            i++;
        }
      }
      fclose(fp);
      if(i>=number_of_instructions){
        block_number++;
        i=0;
       }
       else i++;
       memory.blocks[block_number].page[i].command[0]='\0';
      
     
      //Atualiza a ocupação da memória
      memory.current_occupation += ceil(new->segment_size/BLOCK_SIZE);
     
      //Mem load finish
      printf("\nProcesso %s criado\nid do processo :%d  \nadicionado nos blocos: %d a %d", new->process_name, new->process_id, new->segment_id, (new->segment_id+number_pages-1));
      printf(" da memoria.\n");
     
     //Define o processo como pronto 
      new->process_state = 1;
      //Incializa o Program Counter
      new->PC = 0;
      
      //Insere  processo na lista do BCP de forma ordenada
      BCP* aux = bcp_head;
      while(aux != NULL && aux->remaining_time < new->remaining_time ){
          aux = aux->next;
      }
     
      if(aux == NULL){
        //Se lista vazia
       if (bcp_head == aux) {
         bcp_head = new;
         bcp_tail = bcp_head;
         new->next = NULL;
         new->prev = NULL;
       }
       //se ultima posição
        else
          bcp_tail->next = new;
          new->next = NULL;
          new->prev = bcp_tail;
          bcp_tail = new;
      }
      //se primeira posicao
      else if(aux == bcp_head){
         bcp_head = new;
         new->next = aux;
         new->prev = NULL;
         aux->prev = new;
       }
       else{
         aux->next = aux->next->next;
         new->prev = aux->prev;
         new->next = aux;
       }
    }

     return;
   } 
   
  // Finaliza o processo remove do BCP e da Memória
  // Recebe o id do processo e retorna vazio
  void processFinish(int process_ID) {
      BCP *aux = bcp_head;
      BCP *aux2 = bcp_head;

      //Procura o Processo no BCP
      while(aux!=NULL){
        if (aux->process_id==process_ID){
          //libera os blocos ocupados pelo processo
            for(int i = aux->segment_id ; i< (aux->segment_id + ceil(aux->segment_size/BLOCK_SIZE));i++){
               memory.blocks[i].state=-1;
            }
            char comand = memory.blocks[aux->segment_id].page[0].command[0] ;
           
           //Marca que as paginas podem ser sobre escritas
            if  ( comand != '\0') {
              memory.blocks[aux->segment_id].page[0].command[0]='\0';
            }
      
           //Atualiza a ocupação da memória
           memory.current_occupation -= ceil(aux->segment_size/BLOCK_SIZE);

          //Remove o processo da lista do BCP
           if(aux == bcp_head && aux == bcp_tail){
              bcp_head = NULL;
              bcp_tail = NULL;
              free(aux);
           }
          
           else  if(aux==bcp_head){
             bcp_head = aux->next;
             aux2 = aux->next;
             aux2->prev = NULL;
             free(aux);
           }

             else if(aux==bcp_tail){
               bcp_tail = aux->prev;
               aux2 = aux->prev;
               aux2->next = NULL;
               free(aux);
             }
             else{
               aux2 = aux->prev;
               aux2->next->next->prev = aux2;
               aux2->next = aux2->next->next;
               free(aux);
             }
            //Fim do Process Finish
            printf("Processo terminado e todos os seus Recurso foram desalocados\n");
            return;
           }
           else
              aux = aux->next;
        }
        printf("Processo nao encontrado :(\n");
        return;
    }

//Adiciona a instrução do processo na fila de impressão
// Recebe o Id do processo
// Recebe o comando a ser executado
// Retorna nada
void addFilaImpressao(int process_ID, Instrucao comando){
   Fila_IO *new = malloc(sizeof(Fila_IO));
   Fila_IO *aux = Fila_Impressao;

   new->Process_id = process_ID;
   strcpy(new->tarefa.command, comando.command);
   new->tarefa.number = comando.number;
   new->next = NULL;
   if(Fila_Impressao ==NULL){
     Fila_Impressao = new;
     return;
   }
   while(aux->next!=NULL)
      aux= aux->next;
   aux->next =new;
}

//Adiciona a instrução do processo na fila de Operação no Disco
// Recebe o Id do processo
// Recebe o comando a ser executado
// Retorna nada
void addFilaDISC(int process_ID, Instrucao comando){
   Fila_IO *new = malloc(sizeof(Fila_IO));
   Fila_IO *aux = Fila_disc;

   new->Process_id = process_ID;
   strcpy(new->tarefa.command ,comando.command);
   new->tarefa.number = comando.number;

   new->next = NULL;
   if(Fila_disc ==NULL){
     Fila_disc = new;
     return;
   }
   while(aux->next!=NULL)
      aux= aux->next;
   aux->next =new;
}

//Adiciona o processo ou a fila de impressao ou a do disco
// Recebe o id do processo e o comando a ser executado
// Retorna nada
void entrada_saida(int process_ID, Instrucao comando){
    BCP *aux = bcp_head;
      if(!strcmp(comando.command , "print")){
         sem_wait(&semaphore_3);
          addFilaImpressao(process_ID,comando);
         sem_post(&semaphore_3);
      }
      else if(!strcmp(comando.command ,"write") || !strcmp(comando.command , "read")){
         sem_wait(&semaphore_4);
         addFilaDISC(process_ID,comando);
         sem_post(&semaphore_4);
      }
    return;
}

//Função que realizará a escolha do processo 
// a ser executado
//  Executa o processo
// Faz controle do estado do processo
void* escalonador(void *id){
  BCP *aux = bcp_head;
  int command_list_page,command_list_block;
  BCP *waiting = NULL;
  int id_thread = *(int *)id;
// Bloqueia o processo no momento em que vai escolher o processo a ser executado
  sem_wait(&semaphore);
  //Busca se existe algum processo disponivel para a execução
  while(aux!=NULL && aux->process_state != 1 ){
    if (aux->process_state == -2)
      waiting = aux;
    aux = aux->next;
  } 
   if(aux==NULL){
     if(waiting!=NULL)
        aux = waiting;
       else{
        sem_post(&semaphore);
        pthread_exit(NULL);
       }
        
   }

   aux->process_state = 0;
  
//Libera o processo
   sem_post(&semaphore);

   printf("\n\n---------------------------------\n");
   printf("\nProcesso: %s id:%d esta em execucao na THREAD: %d \n", aux->process_name,aux->process_id,id_thread);
   int bloco = 0;
   while (aux->process_state == 0){

        command_list_block =  aux->segment_id+(aux->PC/number_of_instructions);
        command_list_page = (aux->PC%number_of_instructions);
        // Se o processo chegou ao fim da lista de processos ele foi finalizado
        //Se o Estado do processo é igual a 2 o processo foi finalizado
          if( memory.blocks[command_list_block].page[command_list_page].command[0]== '\0'){
              aux->process_state = 2;
              printf("Processo %s\nid :%d foi finalizado\n", aux->process_name, aux->process_id);
              sem_wait(&semaphore_2);
              processFinish(aux->process_id);
              sem_post(&semaphore_2);
              pthread_exit(NULL);
          }

          while(memory.blocks[command_list_block].page[command_list_page].number>0 && aux->process_state == 0){
            // Caso seja um comando de execução

            switch(memory.blocks[command_list_block].page[command_list_page].command[1]){
              case 'x':
                printf("\nexec\n\n");
                  // Simula o timer interrupt que é feito com um hardware fora da cpu
                  //Evitando o sequestro da CPU
                  while(memory.blocks[command_list_block].page[command_list_page].number>0 ){
                      usleep(SLEEP);
                      memory.blocks[command_list_block].page[command_list_page].number--; // Diminui o tempo de execução do comando
                      aux->remaining_time--;     //Diminui o tempo de execução total do programa
                  }
                  //Caso chegue ao fim do tempo de execução
                    aux->PC++;
                    aux->process_state = 1;
              break;

              case 'r' :
                  /// lidar com E/S
                  printf("PROGRAMA BLOQUEADO POR E/S\n");
                  entrada_saida(aux->process_id, memory.blocks[command_list_block].page[command_list_page]);
                  aux->process_state = -1;//processo passa  a ser blocked
                  pthread_exit(NULL);
                  break;
              
              case 'e' :
                  /// lidar com E/S
                  printf("PROGRAMA BLOQUEADO POR E/S\n");
                  entrada_saida(aux->process_id, memory.blocks[command_list_block].page[command_list_page]);
                  aux->process_state = -1;//processo passa  a ser blocked
                  pthread_exit(NULL);
                  break;

              case '(' :
              sem_wait(&semaphore_5);
              int k=0;
              int temsem;
              if(memory.blocks[command_list_block].page[command_list_page].command[0]== 'P'){
                  // Se o comando for um semaforo P
                  printf("Semaforo P(");
                  //Busca a variavel global desse semaforo
                  while(Semaforos_global[k].id!=memory.blocks[command_list_block].page[command_list_page].command[2])
                      k++;
                  printf("%c), value = %d\n",Semaforos_global[k].id, Semaforos_global[k].value );
                  //Caso o semaforo ainda n esteja bloqueado
                  if(Semaforos_global[k].value>0){
                      Semaforos_global[k].value--;
                      //O programa passa para a próxima instução
                      aux->PC++;
                      aux->remaining_time -= 200;
                      aux->process_state = 0; // Processo passa a ser executado
                      memory.blocks[command_list_block].page[command_list_page].number = 0;
                  }
                  else{
                      printf("Processo Bloqueado Pelo semaforo P(%c)\n",Semaforos_global[k].id );
                      aux->process_state = -2; // O processo passa a estar esperando
                      pthread_exit(NULL);
                }
            }
          else{
              k=0;
              // Busca a variavel global desse semaforo
              while(Semaforos_global[k].id!=memory.blocks[command_list_block].page[command_list_page].command[2])
                  k++;
              printf("Semaforo V(");
              Semaforos_global[k].value++; // Aumenta o valor da variavel Global do semaforo
              printf("%c), value = %d\n",Semaforos_global[k].id, Semaforos_global[k].value );
              aux->PC++;
              aux->remaining_time -= 200;
              aux->process_state = 0; // Processo esta  executando
              memory.blocks[command_list_block].page[command_list_page].number = 0;
          }
          sem_post(&semaphore_5);
        break;
      
        default:
              printf("Comando nao Existe\n");
              aux->process_state = 2;
              pthread_exit(NULL);
         break;
      }
   }
 }
}

 //Busca a instrução na fila de Impressão 
   // caso o tempo de execução da instrução seja maior que o 
   //Tempo passado os valores são apenas atualizados
   // Se o tempo for menor esse processo e removido da fila
   // Esse loop continua ate  a fila estiver vazia ou o tempo acabar
void* Realizando_IO_Impressao(){
   BCP * aux ;
   Fila_IO *Fila_aux;
   int command_list_block, command_list_page;
   //Busca a instrução na fila de Impressão 
   //Executa a impressao 
   // Esse loop continua ate  a fila estiver vazia 
   while(Fila_Impressao!=NULL){
        aux = bcp_head;
        Fila_aux = Fila_Impressao;
        while(aux!=NULL && aux->process_id != Fila_Impressao->Process_id)
               aux = aux->next;
          if(aux == NULL)
            printf("\nERROR\n");
          else{
            printf("\nRealizando Impressao processo id: %d\n", aux->process_id);
            while(Fila_Impressao->tarefa.number>0){
              Fila_Impressao->tarefa.number--;
               aux->remaining_time--;
              usleep(SLEEP);
            }
            sem_wait(&semaphore);          
            aux->process_state=1;
            aux->PC++;
            sem_post(&semaphore);
            if(Fila_Impressao->next==NULL){
                Fila_Impressao = NULL;
                free(Fila_aux);
                pthread_exit(NULL);
            }
            else{
              Fila_Impressao = Fila_Impressao->next;
              free(Fila_aux);
            }       
          }
        }
   pthread_exit(NULL);
 }

    //Busca a instrução na fila de Impressão 
   // caso o tempo de execução da instrução seja maior que o 
   //Tempo passado os valores são apenas atualizados
   // Se o tempo for menor esse processo e removido da fila
   // Esse loop continua ate  a fila estiver vazia ou o tempo acabar
 void* Realizando_IO_Disc(){
   BCP * aux ;
   Fila_IO *Fila_aux;
   int command_list_block, command_list_page;

   while(Fila_disc!=NULL){
        aux = bcp_head;
        Fila_aux = Fila_disc;
        while(aux!=NULL && aux->process_id != Fila_disc->Process_id)
               aux = aux->next;
          if(aux == NULL)
            printf("\nERROR\n");
          else{
            printf("\nRealizando operacao de Disco processo id: %d\n", aux->process_id);
            while(Fila_disc->tarefa.number>0){
              Fila_disc->tarefa.number--;
              usleep(SLEEP);
            }
            aux->remaining_time -= Fila_disc->tarefa.number;
            sem_wait(&semaphore);          
            aux->process_state=1;
            aux->PC++;
            sem_post(&semaphore);
            if(Fila_disc->next==NULL){
                Fila_disc = NULL;
            }
            else{
              Fila_disc = Fila_disc->next;
            }
          free(Fila_aux);
          }
        }
        pthread_exit(NULL);
 }

void print_Process(BCP *process){
  printf("\n\nProcess name: %s", process->process_name);
  printf("\nProcess Id: %d", process->process_id);
  printf("\nProcess State: %d", process->process_state);
  printf("\nProcess Counter : %d", process->PC);
  printf("\nSegment ID:%d", process->segment_id);
  printf("\nPrioridade do Processo: %d", process->process_priority);
  printf("\nTempo restante:%d", process->remaining_time);
  printf("\n\n");
  return;
}

void print_ListBCP(BCP *bcp_head){
   BCP *aux = bcp_head;
   int k=0;
   while(aux!=NULL){
     print_Process(aux);
      aux = aux->next;
   }
}

int kbhit()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

int main() {
  reset();
  //iniciando semaforos
  sem_init(&semaphore, 0, 1);
  sem_init(&semaphore_2, 0, 1);
  sem_init(&semaphore_3, 0, 1);
  sem_init(&semaphore_4, 0, 1);
  sem_init(&semaphore_5, 0 ,1);
  pthread_t thread_processador1,thread_processador2;
  pthread_t thread_Disc,thread_Impressao; 
  char file_name[100];
  int option;
  int k=1;
  int j=1;
  int i =0;
  int id_thread_process1=1,id_thread_process2=2;
  int id_thread_Disc = 3, id_thread_Impressao=4;
  
 while (k)
 {
    while (!kbhit())
    {   
        //Duas threads para o processador como se houvessem 2 nucleos
        pthread_create(&thread_processador1, NULL, escalonador, (void*)&id_thread_process1);
        pthread_create(&thread_processador2, NULL, escalonador, (void*)&id_thread_process2);
        //Threads para entrada e saida
        pthread_create(&thread_Disc, NULL, Realizando_IO_Disc, (void*)&id_thread_Disc);
        pthread_create(&thread_Impressao, NULL, Realizando_IO_Impressao, (void*)&id_thread_Impressao);
        pthread_join(thread_processador1,NULL);
        pthread_join(thread_processador2,NULL);
        pthread_join(thread_Disc,NULL);
        pthread_join(thread_Impressao,NULL);
   
    }
   j=1;
   while(j){
      printf("\n\n\nO que deseja fazer :\n");
      printf("1-Informacoes sobre ocupacao de memoria\n");
      printf("2-Informacoes sobre ocupacao da CPU e a lista de processos\n");
      printf("3-Criar novo processo:\n");
      printf("4- sair do menu\n");
      printf("0- fechar programa\n");
      scanf("%d", & option);
      switch (option)
      {
      case 1:
        printf("\nNumeros de paginas ocupadas: %d de %d paginas\n", memory.current_occupation, (MEMORY_SIZE/BLOCK_SIZE));
        break;

      case 2:
        print_ListBCP(bcp_head);
        break;

      case 3:
        printf("\nDigte o nome do arquivo:");
        scanf("%s", file_name);
        processCreate(file_name);
        break;

      case 4:
        j=0;
        break;

      case 0: 
        k = j= 0; 
        break; 

      default:
        break;
      }
   }
  }
   return 0;
 }
