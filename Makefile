.PHONY: test demo firmware

test:
	python -m unittest discover -s tests -v
	node tests/web/setup_test.cjs
	@mkdir -p .local/validation
	$(CC) -Wall -Wextra -Werror tests/setup_address_test.c -o .local/validation/setup_address_test
	.local/validation/setup_address_test
	$(CC) -Wall -Wextra -Werror tests/fast_packet_test.c -o .local/validation/fast_packet_test
	.local/validation/fast_packet_test
	$(CC) -Wall -Wextra -Werror tests/nec_press_test.c -o .local/validation/nec_press_test
	.local/validation/nec_press_test

demo:
	python -m tools.demo

firmware:
	idf.py -C firmware build
