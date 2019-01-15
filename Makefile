all:
	gcc -g main.c -o fixtb -lavcodec -lavformat -lavfilter -lavdevice -lswresample -lswscale -lavutil -pthread
