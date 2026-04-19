#include <stdio.h>
#include "mpc.h"

#define LASSERT(args, cond, err) \
    if(!(cond)) {lval_del(args); return lval_err(err);}

// uma lval representa qualquer um dos valores que a linguagem pode manipular
// a tipagem mutuamente exclusiva é análoga a implementação "manual" de uma union
typedef struct lval{
    int type; // representa o tipo de valor da lval, sumarizados em enum
    long num;

    char* err;
    char* sym;

    int count; //contagem de filhos
    struct lval** cell; // ponteiro para ponteiros de lval: cada ponteiro apontado representa um filho
} lval;

enum {LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR }; // identificadores de tipo
enum {LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM}; // identificadores de erro

//forward declarations: resolve a dependência circular entre funções
lval* lval_add(lval* v, lval* x);
void lval_print(lval* v);
lval* lval_eval(lval* v);
lval* lval_pop(lval* v, int i);
lval* lval_take(lval* v, int i);
lval* builtin_op(lval* a, char* op);

lval* builtin(lval* a, char* func);
lval* builtin_head(lval* a);
lval* builtin_tail(lval* a);
lval* builtin_list(lval* a);
lval* builtin_eval(lval* a);
lval* builtin_join(lval* a);
lval* lval_join(lval* x, lval* y);

// cria uma lval de tipo número e atribui o valor
lval* lval_num(long x){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

// cria uma lval de tipo erro e atribui uma string à sua chamada
lval* lval_err(char* m){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;
    v->err = malloc(strlen(m) + 1);
    strcpy(v->err, m);
    return v;
}

// cria uma lval de tipo symbol e atribui o operador relativo ao campo
lval* lval_sym(char *s){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(s) + 1 );
    strcpy(v->sym, s);
    return v;
}

//cria uma lval de tipo sexpr e inicia o vetor de filhos em NULL
lval* lval_sexpr(void){
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

//cria um ponteiro para uma nova lval Qexpr vazia
lval* lval_qexpr(void){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

// elimina uma lval e desaloca seu espaço e das informações extras aninhadas
void lval_del(lval* v){
    switch (v->type)
    {
    case LVAL_NUM: break; // o caso num não possui informação extra
    case LVAL_ERR: free(v->err); break; // o caso erro possui mensagem 
    case LVAL_SYM: free(v->sym); break; // o caso simboolo possui o caractere

    // se é Qexpr ou Sexpr delete todos os elementos internos
    case LVAL_QEXPR:
    case LVAL_SEXPR: // o caso sexpr há de liberar memória dos ponteiros para lval
    // além do ponteiros que os aponta (cell).
        for(int i = 0; i < v->count; i++){
            lval_del(v->cell[i]);
        }
        free(v->cell);
    break;
    }

    free(v); // libera memória da struct lval
}

// tenta converter a string do nó da árvore para long e retorna lval_err se o número for
// grande demais, ou lval_num se a conversão for bem sucedida.

/*a biblioteca mpc oferece a struct da árvore sintatica. o input do usuario é uma string. 
a medida que se "destrincha" a string, aparecerá a necessidade de compreender tal como um 
número, quando assim for identificado*/

lval* lval_read_num(mpc_ast_t* t){
/*
mpc_ast_t é a struct da biblioteca mpc que representa um nó da árvore sintática:
;o resultado do parsing.
*/ 
    errno = 0; // variável global especial definida em <errno.h> que funções da biblioteca
//padrão usam para reportar erros. Zeramos-a a fim de validá-la segundo apenas a validação
//de strtol, e não qualquer outra chamada anterior.
    long x = strtol(t->contents, NULL, 10); // converte uma string para long (string to long)
// os três parâmetros são: string alvo, ponteiro para o ponto de parada e a base numérica.
    return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
// ERANGE é uma cosntante de <errno.h> que siginifica _error range_: aponta se o valor não
// cabe no tipo de destino.
 
}

lval* lval_read(mpc_ast_t* t){
    if(strstr(t->tag, "number")) { return lval_read_num(t);      }
    if(strstr(t->tag, "symbol")) { return lval_sym(t->contents); }
    
    lval* x = NULL;
    // determina o nó raiz como uma expressão de expressões aninhadas. 
    if (strcmp(t->tag, ">") == 0) { x = lval_sexpr();} // ">" corresponde à convenção interna
// da biblioteca de tag para nó raíz da árvore sintática.
    // determina qualquer nó de tag sexpr como tal.
    if (strstr(t->tag, "sexpr")) { x = lval_sexpr();}
    if (strstr(t->tag, "qexpr")) { x = lval_qexpr();}

    for (int i = 0; i < t->children_num; i++){
        // adiciona os nós filhos da sexpr
        //ignora tokens estruturais, adiciona apenas nós com valor semânticos
        if(strcmp (t->children[i]->contents, "(") == 0) {continue;}
        if(strcmp (t->children[i]->contents, ")") == 0) {continue;}
        if(strcmp (t->children[i]->contents, "{") == 0) {continue;}
        if(strcmp (t->children[i]->contents, "}") == 0) {continue;}
        if(strcmp (t->children[i]->tag, "regex") == 0)  {continue;}
        x = lval_add(x, lval_read(t->children[i]));
    }
    return x;
}

// adiciona um nó filho
lval* lval_add(lval* v, lval *x){
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count); // realoca espaço para contê-lo
    v->cell[v->count-1] = x; // insere x como filho de v
    return v;
}

// imprime os filhos de v separados por espaço, entre os delimitadores open e close
void lval_expr_print(lval* v, char open, char close){
    putchar(open);
    for(int i =0; i< v->count; i++){
        lval_print(v->cell[i]);

        if(i != v->count -1){
            putchar(' ');
        }
    }
    putchar(close);
}

// seleciona a informação a ser printada segundo o tipo da lval
void lval_print(lval *v){
    switch (v->type){
    case LVAL_NUM   :    printf("%li", v->num);        break;
    case LVAL_ERR   :    printf("Error: %s", v->err);  break;
    case LVAL_SYM   :    printf("%s", v->sym);         break;
    case LVAL_SEXPR :    lval_expr_print(v, '(', ')'); break;
    case LVAL_QEXPR :    lval_expr_print(v, '{', '}'); break;
    }
}

//wrapper para print no terminal
void lval_println(lval* v) { lval_print(v); putchar('\n'); }


lval* lval_eval_sexpr(lval *v){
    for(int i =0; i < v->count; i++){
        // mapeia cada nó filho da sexpr para seu valor
        v->cell[i] = lval_eval(v->cell[i]);
    }

    for(int i = 0; i < v->count; i++){
        if(v->cell[i]->type == LVAL_ERR) {return lval_take(v, i);}
    }

    if(v->count == 0){ return v;}

    if(v->count == 1){return lval_take(v, 0);}

    lval *f = lval_pop(v, 0);
    //trata o caso de expressão não iniada com operador (padrão notaçao polonesa)
    if(f->type != LVAL_SYM){
        lval_del(f); lval_del(v);
        return lval_err("S-expression does not start with a symbol");
    }

    // extrai o segmento de sexpr e seu primeiro operador (f->sym)
    lval* result = builtin(v, f->sym);
    lval_del(f);
    return result;
}

lval* lval_eval(lval *v){
    if(v->type == LVAL_SEXPR) {return lval_eval_sexpr(v);}
    return v;
}

// remove o filho na posição i de v e o retorna, mantendo o restante
lval* lval_pop(lval *v, int i){
    lval* x = v->cell[i];

    //desloca os elementos da array em uma posição para a esquerda
    memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));

    v->count--;

    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    return x;
}

// retorna o filho de posição i e descarta todo o resto
lval* lval_take(lval*v, int i){
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

// toma a head da q-expression
lval* builtin_head(lval* a){
    LASSERT(a, a->count == 1,
        "Function 'head' passed too many arguments!");

    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
        "Function 'head' passed incorrect type.");

    LASSERT(a, a->cell[0]->count != 0,
        "Function 'head' passed {}");

    lval* v = lval_take(a, 0);

    while (v->count > 1) {lval_del(lval_pop(v, 1)); }
    return v;
}

lval* builtin_tail(lval* a){
    LASSERT(a, a->count == 1,
        "Function 'tail' passed too many arguments!");

    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
        "Function 'tail' passed incorrect type.");

    LASSERT(a, a->cell[0]->count != 0,
        "Function 'tail' passed {}");

    lval* v = lval_take(a, 0);

    lval_del(lval_pop(v, 0));
    return v;
}

//converte uma S-expression numa Q-expression
lval* builtin_list(lval* a){
    a->type = LVAL_QEXPR;
    return a;
}

// toma uma q-expression e converte numa s-expression e a evalua
lval* builtin_eval(lval* a){
    LASSERT(a, a->count == 1,
        "Function 'eval' passed too many arguments!");
    LASSERT(a, a->cell[0]->type,
        "Function 'eval' passed incorrect type.");
    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(x);
}

lval* builtin_join(lval *a){
    for(int i = 0; i < a->count; i++){
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
            "Function 'join' passed incorrect type.");
    }

    lval* x = lval_pop(a, 0);

    while(a->count){
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}

lval* lval_join(lval* x, lval* y){
    // adicionar cada célula de y para x
    while(y->count){
        x = lval_add(x, lval_pop(y, 0));
    }

    lval_del(y);
    return x;
}

//recebe uma lista completamente avaliada
lval* builtin_op(lval *a, char *op){
    for(int i = 0; i < a->count; i++){
        if(a->cell[i]->type != LVAL_NUM){
            lval_del(a);
            return lval_err("Cannot operate on non-number.");
        }
    }

    // recebe o primeiro operando
    lval* x = lval_pop(a, 0);

    // opera o "-" unário
    if ((strcmp(op, "-") == 0) && a->count == 0){
        x->num = -x->num;
    }

    while(a->count > 0){
        //toma o próximo operando
        lval* y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) {x->num += y->num;}
        if (strcmp(op, "-") == 0) {x->num -= y->num;}
        if (strcmp(op, "*") == 0) {x->num *= y->num;}
        if (strcmp(op, "/") == 0) {
            if(y->num == 0){
                lval_del(x); lval_del(y);
                x = lval_err("Division by zero"); break;
            }
        x->num /= y->num;
        }

        lval_del(y);
    }

    lval_del(a); return x;
}

lval* builtin(lval *a, char* func){
    if(strcmp("list", func) == 0) {return builtin_list(a);}
    if(strcmp("head", func) == 0) {return builtin_head(a);}
    if(strcmp("tail", func) == 0) {return builtin_tail(a);}
    if(strcmp("join", func) == 0) {return builtin_join(a);}
    if(strcmp("eval", func) == 0) {return builtin_eval(a);}
    if(strstr("+-/*", func))      {return builtin_op(a, func);}
    lval_del(a);
    return lval_err("Unknown function");
}

static char input[2048];

int main(int argc, char** argv){

    // parsers para cada categoria gramatical
    mpc_parser_t* Number    = mpc_new("number");
    mpc_parser_t* Symbol    = mpc_new("symbol");
    mpc_parser_t* Sexpr     = mpc_new("sexpr");
    mpc_parser_t* Qexpr     = mpc_new("qexpr");
    mpc_parser_t* Expr      = mpc_new("expr");
    mpc_parser_t* OLisp     = mpc_new("olisp");

    // especifica a representação das classes sintáticas
    mpca_lang(MPCA_LANG_DEFAULT,
        "                                                           \
        number : /-?[0-9]+/ ;                                       \
        symbol : \"list\" | \"head\" | \"tail\"                     \
               | \"join\" | \"eval\" | '+' | '-' | '*' | '/' ; \
        sexpr  : '(' <expr>* ')' ;                                  \
        qexpr  : '{' <expr>* '}' ;                                  \
        expr   : <number> | <symbol> | <sexpr> | <qexpr> ;          \
        olisp  : /^/ <expr>* /$/ ;                                  \
        ", 
        Number, Symbol, Sexpr, Qexpr, Expr, OLisp);

    puts("OLisp Version 0.0.0.1");
    puts("Press Ctrl+c to Exit\n");

    while(1){ // REPL
        fputs("olisp>" , stdout);
        fgets(input, 2048, stdin);

        mpc_result_t r;
        //tenta parsear o input segundo a gramática OLisp e, se bem sucedido, preenche
        // r.output com a árvore sintática.
        if(mpc_parse("<stdin>", input, OLisp, &r)){
            lval* x = lval_eval(lval_read(r.output)); // converte a árvore sintática em uma lval
            lval_println(x); // imprime o resultado
            lval_del(x); // libera a memória
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }
    }

    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, OLisp);

    return 0;
}