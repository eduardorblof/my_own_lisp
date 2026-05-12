#include "mpc.h"

#ifdef _WIN32

static char buffer[2048];

char* readline(char* prompt){
    fputs(prompt, stdout);
    fgets(buffer, 2048, stdin);
    char* cpy = malloc(strlen(buffer)+1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy)-1] = '\0';
    return cpy;
}

void add_history(char* unused){}

#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

#define LASSERT(args, cond, fmt, ...) \
    if(!(cond)) \
        { lval* err = lval_err(fmt, ##__VA_ARGS__); \
            lval_del(args); \
            return err; \
        }

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
    
    /* BASIC */
    long num;
    char* err;
    char* sym;

    /* function */
    lbuiltin builtin;
    lenv* env;
    lval* formals;
    lval* body;

    /*Expressions*/
    int count; //contagem de filhos
    lval** cell; // ponteiro para ponteiros de lval: cada ponteiro apontado representa um filho
};

struct lenv{
    lenv* par;
    int count;
    char** syms;
    lval** vals;
};

//forward declarations: resolve a dependência circular entre funções
lval* lval_add(lval* v, lval* x);
void lval_del(lval* v);
lval* lval_err(char* fmt, ...);
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
lval* lval_eval_sexpr(lenv* e, lval *v);
// inicializa um environment
// um environment é responsavel por mapear nomes a valores.
// neste projeto, implementaremo-os como duas listas associadas por indice, syms e vals

lenv* lenv_new(void){
    lenv* e = malloc(sizeof(lenv));
    e->par = NULL;
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

    if(e->par){
        return lenv_get(e->par, k);
    } else {
        return lval_err("unbound symbol '%s'", k->sym);
    }
}

lenv* lenv_copy(lenv* e){
    lenv* n = malloc(sizeof(lenv));
    n->par = e->par;
    n->count = e->count;
    n->syms = malloc(sizeof(char*) * n->count);
    n->vals = malloc(sizeof(lval*) * n->count);
    for(int i = 0; i < e->count; i++){
        n->syms[i] = malloc(strlen(e->syms[i]) + 1);
        strcpy(n->syms[i], e->syms[i]);
        n->vals[i] = lval_copy(e->vals[i]);
    } 
    return n;
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

void lenv_def(lenv* e, lval* k, lval* v){
    while(e->par){e = e->par;}
    lenv_put(e, k, v);
}

// cria uma lval de tipo número e atribui o valor
lval* lval_num(long x){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

// cria uma lval de tipo erro e atribui uma string à sua chamada
lval* lval_err(char* fmt, ...){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;

    va_list va;
    va_start(va, fmt);

    v->err = malloc(512);

    vsnprintf(v->err, 511, fmt, va);

    v->err = realloc(v->err, strlen(v->err)+1);

    va_end(va);

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
    v->builtin = func;
    return v;
}

lval* lval_lambda(lval* formals, lval* body){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;

    v->builtin = NULL;

    v->env = lenv_new();

    /*set formals and body*/
    v->formals = formals;
    v->body = body;
    return v;

}

void lenv_del(lenv* e);

// elimina uma lval e desaloca seu espaço e das informações extras aninhadas
void lval_del(lval* v){
    switch (v->type)
    {
    case LVAL_NUM: break; // o caso num não possui informação extra
    case LVAL_FUN: 
        if(!v->builtin){
            lenv_del(v->env);
            lval_del(v->formals);
            lval_del(v->body);
        }
    break;
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
    case LVAL_FUN   :    
        if(v->builtin) {
            printf("<builtin>");
        } else {
            printf("(\\ "); lval_print(v->formals);
            putchar(' '); lval_print(v->body); putchar(')');
        }
    break;
    }
}

char* ltype_name(int t){
    switch(t){
        case LVAL_FUN:   return "Function";
        case LVAL_NUM:   return "Number";
        case LVAL_ERR:   return "Error";
        case LVAL_SYM:   return "Symbol";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
        default:         return "Unknown";
    }
}

//wrapper para print no terminal
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

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

#define LASSERT_TYPE(func, args, index, expect) \
    LASSERT(args, args->cell[index]->type == expect, \
    "Function '%s' passed incorrect type for argument %i." \
    "Got '%s', Expected %s.", \
    func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM(func, args, num) \
        LASSERT(args, args->count == num, \
        "Function '%s' passed incorrect number of arguments. "\
        "Got %i, Expected %i." \
        func, args->count, num)
#define LASSERT_NOT_EMPTY(func, args, index) \
            LASSERT(args, args->cell[index]->count != 0, \
            "Function '%s' passed {} for argument %i.", func, index);

// toma a head da q-expression
lval* builtin_head(lenv* e, lval* a){
    LASSERT_NUM("head", a, 1);
    LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("head", a, 0);

    lval* v = lval_take(a, 0);

    while (v->count > 1) {lval_del(lval_pop(v, 1)); }
    return v;
}

lval* builtin_tail(lenv* e, lval* a){
    LASSERT_NUM("tail", a, 1);
    LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
    LASSERT_NOT_EMPTY("tail", a, 0);


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
    LASSERT_NUM("eval", a, 1);
    LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval *a){
    for(int i = 0; i < a->count; i++){
        LASSERT_TYPE("join", a, i, LVAL_QEXPR);
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
        LASSERT_TYPE(op, a, i, LVAL_NUM);
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

lenv* lenv_copy(lenv* e);

lval* lval_copy(lval* v){
    lval* x = malloc(sizeof(lval));
    x->type = v->type;

    switch(v->type){
        // copia funções e números diretamente
        case LVAL_FUN:
            if(v->builtin){
                x->builtin = v->builtin;
            } else {
                x->builtin = NULL;
                x->env = lenv_copy(v->env);
                x->formals = lval_copy(v->formals);
                x->body = lval_copy(v->body);
            }
        break;
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

lval* builtin_var(lenv* e, lval* a, char* func);

lval* builtin_def(lenv* e, lval* a){
    return builtin_var(e, a, "def");
}

lval* builtin_put(lenv* e, lval* a){
    return builtin_var(e, a, "=");
}

lval* builtin_lambda(lenv* e, lval* a){
    LASSERT_NUM("\\", a, 2);
    LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
    LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

    for(int i = 0; i < a->cell[0]->count; i++){
        LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
        "Cannot define non-symbol. Got %s, Expected %s",
        ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM)); 
    }

    lval* formals = lval_pop(a, 0);
    lval* body = lval_pop(a, 0);
    lval_del(a);

    return lval_lambda(formals, body);
}

lval* builtin_var(lenv* e, lval* a, char* func){
    LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

    lval* syms = a->cell[0];
    for(int i = 0; i < syms->count; i++){
        LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
        "Function '%s' cannot define non-symbol."
        "Got %s, Expected %s.", func,
        ltype_name(syms->cell[i]->type),
        ltype_name(LVAL_SYM));
    }

    LASSERT(a, (syms->count == a->count-1),
        "Function '%s' passed too many arguments for symbols. "
        "Got %i, Expected %i.", func, syms->count, a->count-1);

    for(int i = 0; i < syms->count; i++){
        if(strcmp(func, "def") == 0){
            lenv_def(e, syms->cell[i], a->cell[i+1]);
        }

        if(strcmp(func, "=") == 0){
            lenv_put(e, syms->cell[i], a->cell[i+1]);
        }
    }

    lval_del(a);
    return lval_sexpr();
}

lval* lval_call(lenv* e, lval* f, lval* a){

    if(f->builtin) {return f->builtin(e, a); }

    int given = a->count;
    int total = f->formals->count;

    while(a->count){
        if(f->formals->count == 0){
            lval_del(a); return lval_err(
                "Function passed too many arguments. "
                "Got %i, Expected %i.", given, total);
        }

        lval* sym = lval_pop(f->formals, 0);

        if(strcmp(sym->sym, "&") == 0){
            if(f->formals->count != 1){
                lval_del(a);
                return lval_err( "Function format invalid. "
                "Symbol '&' not followed by single symbol");
            }
            lval* nsym = lval_pop(f->formals, 0);
            lenv_put(f->env, nsym, builtin_list(e, a));
            lval_del(sym); lval_del(nsym);
            break;
        }

        lval* val = lval_pop(a, 0);

        lenv_put(f->env, sym, val);

        lval_del(sym); lval_del(val);
    }

    lval_del(a);

    if(f->formals->count > 0 &&
        strcmp(f->formals->cell[0]->sym, "&") == 0){

            if(f->formals->count != 2){

                return lval_err("Function format invalid. "
                    "Symbol '&' not followed by single symbol. ");
            }
            
            lval_del(lval_pop(f->formals, 0));

            lval* sym = lval_pop(f->formals, 0);
            lval* val = lval_qexpr();

            lenv_put(f->env, sym , val);
            lval_del(sym); lval_del(a);
        }

    if(f->formals->count == 0){
        f->env->par = e;

        return builtin_eval(
            f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
    } else {
        /*função parcialmente evaluada*/
        return lval_copy(f);
    }
}

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
        lval* err = lval_err(
            "S-Expression starts with incorrect type. "
            "Got '%s', Expected '%s'.",
            ltype_name(f->type), ltype_name(LVAL_FUN));
        lval_del(f); lval_del(v);
        return err;    
    }
    // caso seja, chama a função para resgatar o resultado 
    // extrai o segmento de sexpr e seu primeiro operador (f->sym)
    lval* result = lval_call(e, f, v);
    lval_del(f);
    return result;
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

    lenv_add_builtin(e, "def", builtin_def);
    lenv_add_builtin(e, "=", builtin_put);
    lenv_add_builtin(e, "\\", builtin_lambda);
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