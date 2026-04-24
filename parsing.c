#include "mpc.h"

#define LASSERT(args, cond, err) \
    if(!(cond)) {lval_del(args); return lval_err(err);}

// forward declararion de tipos

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

// lisp value
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, 
       LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR }; // identificadores de tipo

// typedef de ponteiro para função
// builtin é um tipo que representa um ponteiro para uma função que
// recebe (lenv*, lval*) e retorna lval*
typedef lval*(*lbuiltin)(lenv*, lval*);

// uma lval representa qualquer um dos valores que a linguagem pode manipular
// a tipagem mutuamente exclusiva é análoga a implementação "manual" de uma union
struct lval{
    int type; // representa o tipo de valor da lval, sumarizados em enum
    
    long num;
    char* err;
    char* sym;
    lbuiltin fun;

    int count; //contagem de filhos
    lval** cell; // ponteiro para ponteiros de lval: cada ponteiro apontado representa um filho
};

struct lenv{
    int count;
    char** syms;
    lval** vals;
};

//forward declarations: resolve a dependência circular entre funções
lval* lval_add(lval* v, lval* x);
void lval_del(lval* v);
lval* lval_err(char* m);
void lval_print(lval* v);
lval* lval_eval(lenv* e, lval* v);
lval* lval_pop(lval* v, int i);
lval* lval_take(lval* v, int i);
lval* builtin_op(lenv* e, lval* a, char* op);

lval* builtin(lenv* e, lval* a, char* func);
lval* builtin_head(lenv* e, lval* a);
lval* builtin_tail(lenv* e,lval* a);
lval* builtin_list(lenv* e,lval* a);
lval* builtin_eval(lenv* e,lval* a);
lval* builtin_join(lenv* e,lval* a);
lval* lval_join(lval* x, lval* y);

lval* lval_copy(lval* v);

// inicializa um environment
// um environment é responsavel por mapear nomes a valores.
// neste projeto, implementaremo-os como duas listas associadas por indice, syms e vals

lenv* lenv_new(void){
    lenv* e = malloc(sizeof(lenv));
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;
    return e;
}

//deleta um environment
void lenv_del(lenv* e){
    for(int i = 0; i < e->count; i++){
        free(e->syms[i]);
        lval_del(e->vals[i]);
    }
    free(e->syms);
    free(e->vals);
    free(e);
}

// retorna o valor de uma variavel
lval* lenv_get(lenv* e, lval* k){
    //itera sobre todos os itens do environment
    for(int i =0; i < e->count; i++){
        //checa por matches de simbolo
        if(strcmp(e->syms[i], k->sym) == 0){
            return lval_copy(e->vals[i]);
        }
    }
    return lval_err("unbound symbol");
}

// armazena um valor numa variavel
// 'e' é o environment onde a variavel será inserida
// 'k' é a chave (key): uma lval do tipo LVAL_SYM, o nome da variável.
// 'v' é o valor (value): pode ser qualquer tipo de lval
void lenv_put(lenv* e, lval* k, lval* v){
    // verifica se a variável já existe
    for(int i = 0; i < e->count; i++){
        // se a variavel for encontrada, delete o item na posição
        // e substitui pela variavel 
        if(strcmp(e->syms[i], k->sym) == 0){
            lval_del(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }

    // se a variavel nao existe, aloca espaço 
    e->count++;
    e->vals = realloc(e->vals, sizeof(lval*) * e->count);
    e->syms = realloc(e->syms, sizeof(char*) * e->count);

    // copia o counteudo da lval e simbolo em uma nova localização
    e->vals[e->count-1] = lval_copy(v);
    e->syms[e->count-1] = malloc(strlen(k->sym)+1);
    strcpy(e->syms[e->count-1], k->sym);
}

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

lval* lval_fun(lbuiltin func){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->fun = func;
    return v;
}

// elimina uma lval e desaloca seu espaço e das informações extras aninhadas
void lval_del(lval* v){
    switch (v->type)
    {
    case LVAL_NUM: break; // o caso num não possui informação extra
    case LVAL_FUN: break;
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
    case LVAL_NUM   :    printf("%li", v->num)       ; break;
    case LVAL_ERR   :    printf("Error: %s", v->err) ; break;
    case LVAL_SYM   :    printf("%s", v->sym)        ; break;
    case LVAL_SEXPR :    lval_expr_print(v, '(', ')'); break;
    case LVAL_QEXPR :    lval_expr_print(v, '{', '}'); break;
    case LVAL_FUN   :    printf("<function>")        ; break;
    }
}

//wrapper para print no terminal
void lval_println(lval* v) { lval_print(v); putchar('\n'); }


lval* lval_eval_sexpr(lenv* e, lval *v){
    for(int i =0; i < v->count; i++){
        // mapeia cada nó filho da sexpr para seu valor
        v->cell[i] = lval_eval(e, v->cell[i]);
    }

    for(int i = 0; i < v->count; i++){
        if(v->cell[i]->type == LVAL_ERR) {return lval_take(v, i);}
    }

    if(v->count == 0){ return v;}
    if(v->count == 1){return lval_take(v, 0);}

    // verifica se o primeiro elemento é uma função após evaluação
    lval *f = lval_pop(v, 0);
    //trata o caso de expressão não iniada com operador (padrão notaçao polonesa)
    if(f->type != LVAL_FUN){
        lval_del(v); lval_del(f);
        return lval_err("first element is not a function");
    }

    // caso seja, chama a função para resgatar o resultado 
    // extrai o segmento de sexpr e seu primeiro operador (f->sym)
    lval* result = f->fun(e, v);
    lval_del(f);
    return result;
}

lval* lval_eval(lenv* e, lval *v){
    if(v->type == LVAL_SYM) {
        lval* x = lenv_get(e, v);
        lval_del(v);
        return x;
    }

    if(v->type == LVAL_SEXPR) {return lval_eval_sexpr(e, v);}
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
lval* builtin_head(lenv* e, lval* a){
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

lval* builtin_tail(lenv* e, lval* a){
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
lval* builtin_list(lenv* e, lval* a){
    a->type = LVAL_QEXPR;
    return a;
}

// toma uma q-expression e converte numa s-expression e a evalua
lval* builtin_eval(lenv* e, lval* a){
    LASSERT(a, a->count == 1,
        "Function 'eval' passed too many arguments!");
    LASSERT(a, a->cell[0]->type,
        "Function 'eval' passed incorrect type.");
    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval *a){
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
lval* builtin_op(lenv* e, lval *a, char *op){
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

lval* builtin_add(lenv* e, lval* a){
    return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a){
    return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a){
    return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a){
    return builtin_op(e, a, "/");
}

lval* builtin(lenv* e, lval *a, char* func){
    if(strcmp("list", func) == 0) {return builtin_list(e, a);}
    if(strcmp("head", func) == 0) {return builtin_head(e, a);}
    if(strcmp("tail", func) == 0) {return builtin_tail(e, a);}
    if(strcmp("join", func) == 0) {return builtin_join(e, a);}
    if(strcmp("eval", func) == 0) {return builtin_eval(e, a);}
    if(strstr("+-/*", func))      {return builtin_op(e, a, func);}
    lval_del(a);
    return lval_err("Unknown function");
}

lval* lval_copy(lval* v){
    lval* x = malloc(sizeof(lval));
    x->type = v->type;

    switch(v->type){

        // copia funções e números diretamente
        case LVAL_FUN: x->fun = v->fun; break;
        case LVAL_NUM: x->num = v->num; break;

        // copia strings usando malloc e strcpy
        case LVAL_ERR:
            x->err = malloc(strlen(v->err) + 1);
            strcpy(x->err, v->err); break;
        
        case LVAL_SYM:
            x->sym = malloc(strlen(v->sym) + 1);
            strcpy(x->sym, v->sym); break; 

        // copia listas copiando cada sub expressao
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            x->count = v->count;
            x->cell = malloc(sizeof(lval*) * x->count);
            for(int i = 0; i < x->count; i++){
                x->cell[i] = lval_copy(v->cell[i]);
            }
        break;
    }
    return x;
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func){
    lval* k = lval_sym(name);
    lval* v = lval_fun(func);
    lenv_put(e, k, v);
    lval_del(k); lval_del(v);
}

void lenv_add_builtins(lenv* e){
    //  funções de lista
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);

    //funções matematicas
    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);

}



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
        symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;                 \
        sexpr  : '(' <expr>* ')' ;                                  \
        qexpr  : '{' <expr>* '}' ;                                  \
        expr   : <number> | <symbol> | <sexpr> | <qexpr> ;          \
        olisp  : /^/ <expr>* /$/ ;                                  \
        ", 
        Number, Symbol, Sexpr, Qexpr, Expr, OLisp);

    puts("OLisp Version 0.0.0.1");
    puts("Press Ctrl+c to Exit\n");

    lenv* e = lenv_new();
    lenv_add_builtins(e);

    while(1){ // REPL
        char* input = readline("olisp> ");
        add_history(input);

        mpc_result_t r;
        //tenta parsear o input segundo a gramática OLisp e, se bem sucedido, preenche
        // r.output com a árvore sintática.

        if(mpc_parse("<stdin>", input, OLisp, &r)){

            lval* x = lval_eval(e, lval_read(r.output)); // converte a árvore sintática em uma lval
            lval_println(x); // imprime o resultado
            lval_del(x);
            
            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        free(input);
    }

    lenv_del(e);

    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, OLisp);

    return 0;
}