#! /bin/bash

# Script to generate ../README and ../src/help.c
# THIS IS A HACK - Run it at your own risk...
# Must be run under bash in order to work.

# Generate README
rm -f ../README
for A in R*.doc D*.doc
do
	csplit --silent $A 2
	mv xx00 header.tmp
	mv xx01 contents.tmp
	tr "A-za-z " - <header.tmp >line.tmp
	cat line.tmp header.tmp line.tmp contents.tmp | fold -s >> ../README
	echo >> ../README
	rm -f *.tmp
done

# Generate help.c
tag="N_"
rm -f ../src/help.c
cp ../copynotice ../src/help.c
echo "/* Automatically generated by gendocs.sh */" >> ../src/help.c
echo "#include <stdio.h>" >> ../src/help.c
echo "#include \"gettext.h\"" >> ../src/help.c
echo "#include \"help.h\"" >> ../src/help.c
echo >> ../src/help.c
echo -n "int help_page_count = " >> ../src/help.c
ls -l O*.doc D*.doc | wc --lines >> ../src/help.c
echo ";" >> ../src/help.c
echo "const char *help_page_titles[] = {" >> ../src/help.c
for A in O*.doc D*.doc
do
	echo -n "     $tag(\"" >> ../src/help.c
	echo -n `head --lines=1 $A` >> ../src/help.c
	echo "\")," >> ../src/help.c
done
echo "     NULL };" >> ../src/help.c
echo >> ../src/help.c
for A in O*.doc D*.doc
do
        N=`expr substr $A 1 3`
	echo "static const char * ${N}_contents[] = {" >> ../src/help.c
	split -l 1 $A
	rm -f xaa
	for B in x*
	do
		L=`cat $B`
		if test "x$L" = "x"
		then
			echo    "     \"\\n\"," >> ../src/help.c
		else
	        	echo -n "     /* xgettext:no-c-format */ $tag(\"" >> ../src/help.c
			cat $B | sed -e "s/\\\"/\\\\\"/g" > temp.tmp
			echo -n "`cat temp.tmp`" >> ../src/help.c
			rm -f temp.tmp
			echo "\\n\")," >> ../src/help.c
		fi
	done
	rm -f x*
	echo "     NULL };" >> ../src/help.c
        echo >> ../src/help.c
done
echo "const char ** help_page_contents[] = {" >> ../src/help.c
for A in O*.doc D*.doc
do
        N=`expr substr $A 1 3`
	echo "     ${N}_contents ," >> ../src/help.c
done
echo "     NULL };" >> ../src/help.c

echo "----"
ls -l ../README ../src/help.c
