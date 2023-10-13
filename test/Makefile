.PHONY: all tests clean

TEST_FILES := $(wildcard tests/*.rb)

tests: $(TEST_FILES)
	@for test in $^; do \
		echo -e "\n\nRunning $$test..." ; \
		ruby -r rubygems -Itests/lib $$test ; \
	done

%: tests/%.rb
	@echo -e "\n\nRunning $<..." ; \
	ruby -r rubygems -Itests/lib $<

clean:
	@rm -f /tmp/Bar.db /tmp/Baz.db /tmp/Foo.db
	@rm -f ./moo

.DEFAULT_GOAL := tests
