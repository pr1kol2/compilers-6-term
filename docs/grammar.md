# MF grammar

```ebnf
Program = { TopLevelDeclaration } ;

TopLevelDeclaration = FunctionDeclaration | DataTypeDeclaration ;

FunctionDeclaration = “defn”, lowerVariable, { lowerVariable }, “=”, “{”, Expression, “}” ;

DataTypeDeclaration = “data”, upperVariable, “=”, “{”, Constructor, { “,”, Constructor }, “}” ;

Constructor = upperVariable, { upperVariable } ;

Expression = Term, { (”+” | “-”), Term } ;

Term = Factor, { (”*” | “/”), Factor } ;

Factor = Application ;

Application = Primary, { Primary } ;

Primary = num | lowerVariable | upperVariable | “(”, Expression, “)” | CaseExpression ;

CaseExpression = “case”, Expression, “of”, “{”, Branch, { Branch }, “}” ;

Branch = Pattern, “->”, “{“, Expression, “}” ;

Pattern = lowerVariable | upperVariable, { lowerVariable } ;
```

## Terminals (Tokens)

| Terminal | Regex | Description |
| - | - | - |
| `IntLiteral` | `[0-9]+` | Integer literal used in arithmetic expressions |
| `LowerVariable` | `[a-z][a-zA-Z]*` | Identifier starting with lowercase letter: variables, functions, arguments |
| `UpperVariable` | `[A-Z][a-zA-Z]*` | Identifier starting with uppercase letter: data types and constructors |
| `Definition` | `defn` | Keyword for function definition |
| `Data` | `data` | Keyword for algebraic data type declaration |
| `Case` | `case` | Keyword for pattern matching |
| `Of` | `of` | Keyword separating expression and branches in case |
| `Equal` | `=` | Assignment operator in declarations |
| `Arrow` | `->` | Arrow separating pattern and expression in case branch |
| `Plus` | `+` | Addition operator |
| `Minus` | `-` | Subtraction operator |
| `Star` | `*` | Multiplication operator |
| `Slash` | `/` | Division operator |
| `LeftBrace` | `{` | Opening brace for function body and case branches |
| `RightBrace` | `}` | Closing brace |
| `LeftParentheses` | `(` | Opening parenthesis |
| `RightParentheses` | `)` | Closing parenthesis |
| `Comma` | `,` | Separator for constructors |

## References

- [Extended Backus–Naur form](https://en.wikipedia.org/wiki/Extended_Backus–Naur_form)
