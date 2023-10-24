.PHONY: all tests clean

TEST_FILES := $(wildcard tests/*.rb)

tests:
	exit_status=0; \
	for test in $(TEST_FILES) ; do \
		echo "\n\nRunning $$test..." ; \
		ruby -r rubygems -Itests/lib $$test ; \
		test_exit_code=$$? ; \
		if [ $$test_exit_code -ne 0 ]; then \
			echo "Test $$test failed with exit code $$test_exit_code"; \
			exit_status=$$test_exit_code; \
		fi; \
	done; \
	exit $$exit_status

%: tests/%.rb
	@echo "\n\nRunning $<..." ; \
	ruby -r rubygems -Itests/lib $<

clean:
	@rm -f /tmp/Bar.db /tmp/Baz.db /tmp/Foo.db
	@rm -f ./moo

.DEFAULT_GOAL := tests
