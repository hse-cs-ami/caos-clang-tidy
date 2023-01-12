const int foo = 123;  // should not trigger any warnings. "const" is a compile-time constant.
const float foo2 = 123.4;

struct bad_struct {  // should trigger a warning (bad case)
    int x;
};

union bad_union {  // should trigger a warning (bad case)
    int i;
};

struct GoodStruct {
    int x;
    int y;
};

union GoodUnion {
    int i;
    float f;
};

int main() {
    int bar = foo;
    int qwe = bar + 321;  // should trigger a warning (magic number)

    // should not trigger any warnings
    const struct GoodStruct ST = { .x = 123, .y = 456 };  // literals are allowed in initializer lists
    const union GoodUnion UN = { .i = 123 };
    const int SOME_INTS[] = { 123, 456 };
    const int SOME_MORE_INTS[][2] = {{ 123, 456 }, { 789, 987 }, { 111, 222 } };
}
