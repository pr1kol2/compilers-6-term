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

Branch = Pattern, “->”, Expression ;

Pattern = lowerVariable | upperVariable, { lowerVariable } ;
```

## Terminals (Tokens)

| Terminal | Regex | Description |
|----------|-------|-------------|
| `num` | `[0-9]+` | Integer literal used in arithmetic expressions |
| `lowerVariable` | `[a-z][a-zA-Z]*` | Identifier starting with lowercase letter: variables, functions, arguments |
| `upperVariable` | `[A-Z][a-zA-Z]*` | Identifier starting with uppercase letter: data types and constructors |
| `defn` | `defn` | Keyword for function definition |
| `data` | `data` | Keyword for algebraic data type Declarationaration |
| `case` | `case` | Keyword for pattern matching |
| `of` | `of` | Keyword separating expression and branches in case |
| `=` | `=` | Assignment operator in Declarationarations |
| `->` | `->` | Arrow separating pattern and expression in case branch |
| `+` | `+` | Addition operator |
| `-` | `-` | Subtraction operator |
| `*` | `*` | Multiplication operator |
| `/` | `/` | Division operator |
| `{` | `{` | Opening brace for function body and case branches |
| `}` | `}` | Closing brace |
| `(` | `(` | Opening parenthesis |
| `)` | `)` | Closing parenthesis |
| `,` | `,` | Separator for constructors |

## References

- [Extended Backus–Naur form](https://en.wikipedia.org/wiki/Extended_Backus–Naur_form)

