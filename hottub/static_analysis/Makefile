

.PHONY: all build clean

space:=
space+=

LIBS=$(wildcard libs/*.jar)
CLASSPATH=$(subst $(space),:,$(LIBS))
SOURCES=$(wildcard src/*.java)
CLASSES=$(SOURCES:%.java=%.class)

all: build

clean:
	rm -rf src/*.class
	rm -rf src/*.jar

build: src/static_analysis.jar
	rm -rf build
	mkdir build
	cp $(LIBS) build/
	cp $< build/
	cp src/run.sh build/

src/static_analysis.jar: $(CLASSES)
	# for some reason, `jar` and `javac` return 1 even on success
	jar -cf "$@" $(CLASSES) || [ $$? -eq 1 ]

src/%.class: src/%.java
	# for some reason, `jar` and `javac` return 1 even on success
	javac -Xlint -cp "$(CLASSPATH)" "$<" || [ $$? -eq 1 ]
