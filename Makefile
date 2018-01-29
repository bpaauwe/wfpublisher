#
# Build the WeatherFlow Publisher program
#

VERSION=0.1

CFLAGS=-Wall -Wstrict-prototypes -g

SOURCE= \
		wfpublish.c \
		wfp-rainfall.c \
		wfp-send.c \
		wfp-wbug.c \
		wfp-wunderground.c \
		wfp-cwop.c \
		wfp-pws.c \
		wfp.h \
		cJSON.c \
		cJSON.h
	

OBJECTS= \
		 wfpublish.o \
		 wfp-rainfall.o \
		 wfp-send.o \
		 wfp-wbug.o \
		 wfp-wunderground.o \
		 wfp-pws.o \
		 wfp-cwop.o \
		 cJSON.o
		

MYSQL=-L/usr/lib64/mysql -lmysqlclient -lpthread -lm

all: wfpublish


wfpublish: $(OBJECTS)
	cc -o wfpublish -g $(OBJECTS) $(MYSQL)

install: wfpublish 
	cp wfpublish /usr/local/bin

clean:
	rm -f wfpublish $(OBJECTS)

tgz:
	tar -cvzf wfpublish-$(VERSION).tgz $(SOURCE) Makefile README

zip:
	zip wfpublish-$(VERSION).zip $(SOURCE) Makefile README

.c.o :
	    $(CC) $(CFLAGS) -c $<


