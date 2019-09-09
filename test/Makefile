.PHONY: all tests

tests:
	for test in tests/*.rb ; do \
	  echo "\n\nRunning $$test..." ; \
	  ruby -r rubygems -Itests/lib $$test ; \
	done
