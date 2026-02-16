#include "test.h"
#include "spec/model.h"
#include "spec/parser.h"
#include "spec/validate.h"

/* Forward declarations for test modules */
void test_parser(void);
void test_codegen(void);
void test_integration(void);

void test_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    test_parser();
    test_codegen();
    test_integration();
}
