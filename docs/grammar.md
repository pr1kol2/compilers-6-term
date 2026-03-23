# Grammar

```ebnf
Program                    ::= FunctionDeclaration

FunctionDeclaration        ::= 'fun' Identifier '(' ')' ':' Type '='
                               Expression

LetExpression              ::= 'let' (Declaration (';' Declaration)*)?
                               'in'  Expression (';' Expression)*
                               'end'

Declaration                ::= 'val' Identifier '=' 'ref' Expression


Expression                 ::= AssignExpression

AssignExpression           ::= DisjunctionExpression (':=' DisjunctionExpression)*

DisjunctionExpression      ::= ConjunctionExpression ('orelse' DisjunctionExpression)?

ConjunctionExpression      ::= ComparisonExpression  ('andalso' ConjunctionExpression)?

ComparisonExpression       ::= AdditiveExpression (ComparisonOperator AdditiveExpression)?

AdditiveExpression         ::= MultiplicativeExpression (AdditiveOperator MultiplicativeExpression)*

MultiplicativeExpression   ::= UnaryExpression (MultiplicativeOperator UnaryExpression)*

UnaryExpression            ::= 'not' UnaryExpression
                             | '~'   UnaryExpression
                             | '!'   AtomicExpression
                             | AtomicExpression

AtomicExpression           ::= IntegerConstant
                             | BooleanConstant
                             | Identifier
                             | IfExpression
                             | WhileExpression
                             | PrintExpression
                             | ParenthesizedExpression
                             | SequenceExpression
                             | LetExpression

IfExpression               ::= 'if'    Expression
                               'then'  Expression
                               'else'  Expression

WhileExpression            ::= 'while' Expression
                               'do'    Expression

PrintExpression            ::= 'print' Expression

ParenthesizedExpression    ::= '(' Expression ')'

SequenceExpression         ::= '(' Expression (';' Expression)+ ')'


ComparisonOperator         ::= '=' | '<>' | '<' | '>' | '<=' | '>='
AdditiveOperator           ::= '+' | '-'
MultiplicativeOperator     ::= '*' | 'div' | 'mod'

Type                       ::= 'int' | 'bool' | 'unit'

IntegerConstant            ::= '~'? Digit+
BooleanConstant            ::= 'true' | 'false'
Identifier                 ::= Letter (Letter | Digit | "'" | '_')*

Keyword                    ::= 'fun' | 'val' | 'ref'
                             | 'let' | 'in'  | 'end'
                             | 'if'  | 'then' | 'else'
                             | 'while' | 'do'
                             | 'print'
                             | 'not' | 'orelse' | 'andalso'
                             | 'true' | 'false'
                             | 'int'  | 'bool' | 'unit'
                             | 'div'  | 'mod'

Letter                     ::= [a-zA-Z]
Digit                      ::= [0-9]
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
| `if` | keyword | opens a conditional expression |
| `then` | keyword | separates the condition from the true branch |
| `else` | keyword | opens the false branch — mandatory, `if` without `else` is forbidden |
| `while` | keyword | opens a loop expression |
| `do` | keyword | separates the loop condition from the loop body |
| `print` | keyword | built-in output function |
| `not` | keyword | logical negation |
| `andalso` | keyword | lazy logical AND |
| `orelse` | keyword | lazy logical OR |
| `true` | keyword | boolean literal true |
| `false` | keyword | boolean literal false |
| `int` | keyword | integer type |
| `bool` | keyword | boolean type |
| `unit` | keyword | unit type — the type of expressions that return no meaningful value |
| `div` | keyword | integer division (rounds toward negative infinity) |
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
| `~` | operator | unary minus (SML-style, distinct from binary `-`) |
| `!` | operator | dereference — reads the value stored in a `ref` cell |
| `(` | delimiter | opening parenthesis |
| `)` | delimiter | closing parenthesis |
| `;` | delimiter | expression separator |
| `:` | delimiter | separates name from type in a type annotation |
| `Identifier` | lexeme | variable or function name: `[a-zA-Z][a-zA-Z0-9'_]*` |
| `IntegerConstant` | lexeme | integer literal: `~?[0-9]+` |
| `BooleanConstant` | lexeme | boolean literal: `true` or `false` |
