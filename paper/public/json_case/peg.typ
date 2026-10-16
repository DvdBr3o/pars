#set page(width: auto, height: auto, margin: 4pt)

$
  "json" & ::= "object" | "array" | "string" | "number" | \""true"\" | \""false"\" | #"null" \
  "object" & ::= \'"{"\' "ws" "members"? \'"}"\' \
  "members" & ::= "member" ( \',\'  "member")\* \
  "memeber" & ::= "string" "ws" \'":"\' "ws" "value" \
  "array" & ::=  \'"["\' "ws" ( "value" (\'","\' "value") \* )? "ws" \'"]"\'  \
  "string" & ::= \'"\""\' (\'"\\"\' . | !(\'"\""\' | \'"\\"\' ) .)\* \'"\""\' \
  "number" & ::= \'"-"\'? (\'0\' | (\[\'1\',\'9\'\] \[\'0\',\'9\'\]\* )) (\'.\' "digits")? ((\'e\'|\'E\') (\'+\' | \'-\')? "digits")? \
  "digits" & ::= \[\'0\', \'9\'\]+ \
  "ws" & ::= (\'quad\' | \'"\\t"\'| \'"\\n"\'| \'"\\r"\')
$