# Grammar

```ebnf
Program                    ::= FunctionDeclaration

FunctionDeclaration        ::= 'fun' Identifier '(' ')' ':' Type '='
                               LetExpression

LetExpression              ::= 'let' Declaration*
                               'in'  Statement (';' Statement)*
                               'end'

Declaration                ::= 'val' Identifier '=' 'ref' Expression ';'


Statement                  ::= AssignStatement
                             | IfStatement
                             | WhileStatement
                             | PrintStatement

AssignStatement            ::= Identifier ':=' Expression

IfStatement                ::= 'if'    Expression
                               'then'  '(' Statement (';' Statement)* ')'
                               'else'  '(' Statement (';' Statement)* ')'

WhileStatement             ::= 'while' Expression
                               'do'    '(' Statement (';' Statement)* ')'

PrintStatement             ::= 'print' '(' Expression ')'


Expression                 ::= DisjunctionExpression

DisjunctionExpression      ::= ConjunctionExpression ('orelse'  ConjunctionExpression)*

ConjunctionExpression      ::= NegationExpression    ('andalso' NegationExpression)*

NegationExpression         ::= 'not' NegationExpression
                             | ComparisonExpression

ComparisonExpression       ::= AdditiveExpression (ComparisonOperator AdditiveExpression)?

AdditiveExpression         ::= MultiplicativeExpression (AdditiveOperator MultiplicativeExpression)*

MultiplicativeExpression   ::= UnaryExpression (MultiplicativeOperator UnaryExpression)*

UnaryExpression            ::= '~' UnaryExpression
                             | '!' AtomicExpression
                             | AtomicExpression

AtomicExpression           ::= IntegerConstant
                             | BooleanConstant
                             | Identifier
                             | '(' Expression ')'


ComparisonOperator         ::= '=' | '<>' | '<' | '>' | '<=' | '>='
AdditiveOperator           ::= '+' | '-'
MultiplicativeOperator     ::= '*' | 'div' | 'mod'

Type                       ::= 'int' | 'bool'

IntegerConstant            ::= '~'? Digit+
BooleanConstant            ::= 'true' | 'false'
Identifier                 ::= Letter (Letter | Digit | "'" | '_')*

Letter                     ::= [a-zA-Z]
Digit                      ::= [0-9]

Keyword                    ::= 'fun' | 'val' | 'ref'
                             | 'let' | 'in'  | 'end'
                             | 'if'  | 'then' | 'else'
                             | 'while' | 'do'
                             | 'print'
                             | 'not' | 'orelse' | 'andalso'
                             | 'true' | 'false'
                             | 'int'  | 'bool'
                             | 'div'  | 'mod'
```

## Terminals

| Terminal | Kind | Description |
| --- | --- | --- |
| `fun` | keyword | opens a function declaration |
| `val` | keyword | opens a variable declaration |
| `ref` | keyword | creates a mutable reference cell |
| `let` | keyword | opens the declaration block |
| `in` | keyword | separates declarations from statements |
| `end` | keyword | closes the `let`/`in` block |
| `if` | keyword | opens a conditional statement |
| `then` | keyword | separates the condition from the true branch |
| `else` | keyword | opens the false branch — mandatory, `if` without `else` is forbidden |
| `while` | keyword | opens a loop statement |
| `do` | keyword | separates the loop condition from the loop body |
| `print` | keyword | built-in output function |
| `not` | keyword | logical negation |
| `andalso` | keyword | lazy logical AND |
| `orelse` | keyword | lazy logical OR |
| `true` | keyword | boolean literal TRUE |
| `false` | keyword | boolean literal FALSE |
| `int` | keyword | integer type |
| `bool` | keyword | boolean type |
| `div` | keyword | integer division (rounds down to the nearest integer) |
| `mod` | keyword | remainder (sign follows the divisor) |
| `:=` | operator | assignment — writes a new value into a `ref` cell |
| `=` | operator | equality comparison |
| `<>` | operator | inequality comparison |
| `<` | operator | less than |
| `>` | operator | greater than |
| `<=` | operator | less than or equal |
| `>=` | operator | greater than or equal |
| `+` | operator | addition |
| `-` | operator | subtraction |
| `*` | operator | multiplication |
| `~` | operator | unary minus (distinct from binary `-`) |
| `!` | operator | dereference — reads the value stored in a `ref` cell |
| `(` | delimiter | opening parenthesis |
| `)` | delimiter | closing parenthesis |
| `;` | delimiter | statement and declaration separator |
| `:` | delimiter | separates name from type in a type annotation |
| `Identifier` | lexeme | variable or function name: `[a-zA-Z][a-zA-Z0-9'_]*` |
| `IntegerConstant` | lexeme | integer literal: `~?[0-9]+` |
| `BooleanConstant` | lexeme | boolean literal: `true` or `false` |
