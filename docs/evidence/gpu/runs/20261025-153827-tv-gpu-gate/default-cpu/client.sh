#!/bin/sh
printf "\033[2J\033[H KSTB6077 GPU GUI TEST\n"
i=0
while [ "$i" -lt 30 ]; do
 printf "\033[2;1HFrame %04d  default-cpu\n\033[41m RED \033[42m GREEN \033[44m BLUE \033[0m\n" "$i"
 i=$((i+1)); sleep .1
done
