#include <fcntl.h>

int myopen(char*, int, int);

int main() {
    int fd1 = open("kek", O_RDWR|O_CREAT, 0666);  // No warnings.

    // The check should work correctly if number of args is less than position in config.
    int fd2 = open("kek", O_RDWR|O_CREAT);

    int fd3 = myopen("kek", O_RDWR|O_CREAT, 0666);  // Should trigger a warning - not an ignored function

    int fd4 = open("kek", 66, S_IRUSR|S_IWUSR);  // Warning - 2nd arg is not in ignored list
    int fd5 = open("kek", O_RDWR|O_CREAT, 438);  // Warning - only octal literals are allowed
}
