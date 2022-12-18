const int foo = 123;  // should trigger a warning (magic number, "const" is not a constant)

struct bad_struct {  // should trigger a warning (bad case)
    int x;
};

union bad_union {  // should trigger a warning (bad case)
    int i;
};

int main() {
    int bar = foo;
    int qwe = bar + 321;  // should trigger a warning (magic number)
    return qwe % 2;
}
