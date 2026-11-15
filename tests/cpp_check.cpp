// Compiled with g++ -fsyntax-only: proves usfm.h is valid C++.
#define USFM_IMPLEMENTATION
#include "../usfm.h"

int main()
{
    USFM_Err err;
    USFM_Ast *ast = USFM_Ast_Parse("\\id GEN\n", 8, nullptr, &err);
    USFM_Ast_Free(ast);
    return 0;
}
