all: bin

bin = map wc main
%: %.c; gcc -Wall -g -O0 $^ -o $@
clean:; -rm $(bin)
bin: $(bin)
