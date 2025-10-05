from .lexer import lexer, tokens
import ply.yacc as yacc
# import sympy as sp
import z3
from ..core.recurrence import Recurrence

def p_recurrence(p):
    '''recurrence : initialization if'''
    # p[0] = Recurrence(p[1], p[2])
    if len(p[1]) == 0:
        p[0] = Recurrence(p[2])
    else:
        p[0] = Recurrence.mk_loop_recurrence(p[1], p[2])

def p_initialization(p):
    '''initialization : assignments'''
    p[0] = p[1]

def p_assignments_1(p):
    '''assignments : assignment assignments'''
    p[0] = p[1] | p[2]

def p_assignments_2(p):
    '''assignments : '''
    p[0] = {}

def p_assignment(p):
    '''assignment : lhs ASSIGN expression SEMI'''
    p[0] = {p[1]: p[3]}

def p_expression_plus(p):
    '''expression : expression PLUS term'''
    p[0] = p[1] + p[3]

def p_expression_minus(p):
    '''expression : expression MINUS term'''
    p[0] = p[1] - p[3]

def p_expression_term(p):
    '''expression : term'''
    p[0] = p[1]

def p_term_times(p):
    '''term : term TIMES factor'''
    p[0] = p[1] * p[3]

def p_term_div(p):
    '''term : term DIV factor'''
    p[0] = p[1] / p[3]

def p_term_mod(p):
    '''term : term MOD factor'''
    p[0] = p[1] % p[3]

def p_term_factor(p):
    '''term : factor'''
    p[0] = p[1]

def p_factor_num(p):
    '''factor : NUMBER'''
    # p[0] = sp.Integer(p[1])
    p[0] = z3.IntVal(p[1])

def p_factor_app(p):
    '''factor : ID LPAREN expression_list RPAREN'''
    args = p[3]
    # f = sp.Function(p[1], nargs=len(args))
    sorts = [z3.IntSort()]*(len(args) + 1)
    f = z3.Function(p[1], *sorts)
    p[0] = f(*args)

def p_factor_id(p):
    '''factor : ID'''
    # p[0] = sp.Symbol(p[1], integer=True)
    p[0] = z3.Int(p[1])


def p_factor_negative(p):
    '''factor : MINUS factor'''
    p[0] = -p[2]

def p_factor_paren(p):
    '''factor : LPAREN expression RPAREN'''
    p[0] = p[2]

def p_if_1(p):
    '''if : IF LPAREN condition RPAREN LBRACE assignments RBRACE'''
    cond = p[3]
    assignments = p[6]
    p[0] = [(cond, assignments)]

def p_if_2(p):
    '''if : IF LPAREN condition RPAREN LBRACE assignments RBRACE else'''
    cond = p[3]
    assignments = p[6]
    p[0] = [(cond, assignments)] + p[8]

def p_else_1(p):
    '''else : ELSE LBRACE assignments RBRACE'''
    p[0] = [(z3.BoolVal(True), p[3])]

def p_else_2(p):
    '''else : ELSE if'''
    p[0] = p[2]

def p_condition_atom_GT(p):
    '''condition_atom : expression GT expression'''
    p[0] = p[1] > p[3]

def p_condition_atom_GE(p):
    '''condition_atom : expression GE expression'''
    p[0] = p[1] >= p[3]

def p_condition_atom_LT(p):
    '''condition_atom : expression LT expression'''
    p[0] = p[1] < p[3]

def p_condition_atom_LE(p):
    '''condition_atom : expression LE expression'''
    p[0] = p[1] <= p[3]

def p_condition_atom_EQ(p):
    '''condition_atom : expression EQ expression'''
    # p[0] = sp.Eq(p[1], p[3])
    p[0] = p[1] == p[3]

def p_condition_atom_NE(p):
    '''condition_atom : expression NE expression'''
    # p[0] = sp.Ne(p[1], p[3])
    p[0] = p[1] != p[3]

def p_condition_atom_TRUE(p):
    '''condition_atom : TRUE'''
    p[0] = z3.BoolVal(True)

def p_condition_atom_FALSE(p):
    '''condition_atom : FALSE'''
    p[0] = z3.BoolVal(False)

def p_condition_term_atom(p):
    '''condition_term : condition_atom'''
    p[0] = p[1]

def p_condition_term_paren(p):
    '''condition_term : LPAREN condition RPAREN'''
    p[0] = p[2]

def p_condition_list_1(p):
    '''condition_list : condition COMMA condition_list'''
    p[0] = [p[1]] + p[3]

def p_condition_list_2(p):
    '''condition_list : condition'''
    p[0] = [p[1]]

def p_condition_single(p):
    '''condition : condition_term'''
    p[0] = p[1]

def p_condition_and(p):
    '''condition : AND LPAREN condition_list RPAREN'''
    p[0] = z3.And(*p[3])

def p_condition_or(p):
    '''condition : OR LPAREN condition_list RPAREN'''
    p[0] = z3.Or(*p[3])

def p_condition_neg(p):
    '''condition : NEG LPAREN condition_list RPAREN'''
    p[0] = z3.Not(*p[3])

def p_lhs(p):
    '''lhs : ID LPAREN expression_list RPAREN'''
    args = p[3]
    # f = sp.Function(p[1], nargs=len(args))
    sorts = [z3.IntSort()]*(len(args) + 1)
    f = z3.Function(p[1], *sorts)
    p[0] = f(*args)

def p_expression_list_1(p):
    '''expression_list : expression'''
    p[0] = [p[1]]

def p_expression_list_2(p):
    '''expression_list : expression COMMA expression_list'''
    p[0] = [p[1]] + p[3]


def p_error(p):
    print("Syntax error in input: %s: (%s)" % (p, p.type))


# Build the parser
parser = yacc.yacc()

def parse_str(s):
    return parser.parse(s)

def parse_file(filename):
    with open(filename) as fp:
        return parser.parse(fp.read())