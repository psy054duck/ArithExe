from .lexer import lexer, tokens
import ply.yacc as yacc
import sympy as sp
from ..core.recurrence import Recurrence

def p_recurrence(p):
    '''recurrence : base_cases recursive_cases'''
    p[0] = Recurrence(p[1], p[2])

def p_base_cases(p):
    '''base_cases : assignments'''
    p[0] = p[1]

def p_recursive_cases(p):
    '''recursive_cases : if_statements'''
    p[0] = p[1]

def p_assignments_1(p):
    '''assignments : assignment assignments'''
    p[0] = p[1] | p[2]

def p_assignments_2(p):
    '''assignments : '''
    p[0] = {}

def p_assignment_part(p):
    '''assignment_part : lhs ASSIGN expression'''
    p[0] = {p[1]: p[3]}

def p_assignment_empty_end(p):
    '''assignment_end : SEMI'''
    p[0] = sp.true

def p_assignment_conditional_end(p):
    '''assignment_end : if SEMI'''
    p[0] = p[1]

def p_assignment(p):
    '''assignment : assignment_part assignment_end'''
    p[0] = p[1]

# def p_assignment_without_guard(p):
#     '''assignment : assignment_part SEMI'''
#     p[0] = p[1]
# 
# def p_assignment_with_guard(p):
#     '''assignment : assignment_part assignment_guard SEMI'''
#     p[0] = p[1]

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
    p[0] = sp.Integer(p[1])

def p_factor_app(p):
    '''factor : ID LPAREN expression_list RPAREN'''
    args = p[3]
    f = sp.Function(p[1], nargs=len(args))
    p[0] = f(*args)

def p_factor_id(p):
    '''factor : ID'''
    p[0] = sp.Symbol(p[1], integer=True)


def p_factor_negative(p):
    '''factor : MINUS factor'''
    p[0] = -p[1]

def p_factor_paren(p):
    '''factor : LPAREN expression RPAREN'''
    p[0] = p[2]

def p_if(p):
    '''if : IF LPAREN condition RPAREN'''
    p[0] = p[3]

def p_if_1(p):
    '''if_statements : if LBRACE assignments RBRACE'''
    cond = p[1]
    assignments = p[3]
    p[0] = [(cond, assignments)]

def p_if_2(p):
    '''if_statements : if LBRACE assignments RBRACE else'''
    cond = p[1]
    assignments = p[3]
    p[0] = [(cond, assignments)] + p[5]

def p_else_1(p):
    '''else : ELSE LBRACE assignments RBRACE'''
    p[0] = [(sp.true, p[3])]

def p_else_2(p):
    '''else : ELSE if_statements'''
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
    p[0] = sp.Eq(p[1], p[3])

def p_condition_atom_NE(p):
    '''condition_atom : expression NE expression'''
    p[0] = sp.Ne(p[1], p[3])

def p_condition_atom_TRUE(p):
    '''condition_atom : TRUE'''
    p[0] = sp.true

def p_condition_atom_FALSE(p):
    '''condition_atom : FALSE'''
    p[0] = sp.false

def p_condition_factor_1(p):
    '''condition_factor : condition_atom'''
    p[0] = p[1]

def p_condition_factor_2(p):
    '''condition_factor : NEG condition'''
    p[0] = sp.Not(p[2])

def p_condition_factor_3(p):
    '''condition_factor : LPAREN condition RPAREN'''
    p[0] = p[2]

def p_condition_term_1(p):
    '''condition_term : condition_factor AND condition_term'''
    p[0] = sp.And(p[1], p[3])

def p_condition_term_2(p):
    '''condition_term : condition_factor'''
    p[0] = p[1]

def p_condition_1(p):
    '''condition : condition_term'''
    p[0] = p[1]

def p_condition_2(p):
    '''condition : condition_term OR condition'''
    p[0] = sp.Or(p[1], p[2])

def p_lhs(p):
    '''lhs : ID LPAREN expression_list RPAREN'''
    args = p[3]
    f = sp.Function(p[1], nargs=len(args))
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