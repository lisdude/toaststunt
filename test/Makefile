.PHONY: all tests clean

tests:
	for test in tests/*.rb ; do \
		echo -e "\n\nRunning $$test..." ; \
		ruby -r rubygems -Itests/lib $$test ; \
		done

clean:
	rm -f /tmp/Bar.db /tmp/Baz.db /tmp/Foo.db /tmp/*.db.PANIC
	rm ./moo

all: tests

.DEFAULT_GOAL := tests
