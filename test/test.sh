#!/bin/bash

# Generating reference files
echo "Generating reference files..."
if [ ! -f test/ref_499.bin ]; then
    dd if=test/output.bin of=test/ref_499.bin bs=1 skip=500 count=499 status=none
fi
if [ ! -f test/ref_73456.bin ]; then
    dd if=test/output.bin of=test/ref_73456.bin bs=1 skip=50000 status=none
fi
if [ ! -f test/ref_10000.txt ]; then
    dd if=test/output.txt of=test/ref_10000.txt bs=1 skip=50000 count=10000 status=none
fi
if [ ! -f test/ref_73456.txt ]; then
    dd if=test/output.txt of=test/ref_73456.txt bs=1 skip=50000 status=none
fi

# Performing tests with curl
echo "Performing tests with curl..."

# http1.1 tests
# Full file
curl --ipv4 --http1.1 --dump-header test/ref_header1.out http://hauck-thomas.de/output.bin -o test/ref_file1.out
diff -q test/output.bin test/ref_file1.out && echo "Http 1.1 Full file test passed." || echo "Http 1.1 Full file test failed!"

curl --ipv4 --http1.1 --compressed --dump-header test/ref_header2.out http://hauck-thomas.de/output.txt -o test/ref_file2.txt
diff -q test/output.txt test/ref_file2.txt && echo "Http 1.1 Full compressed(br) file test passed." || echo "Http 1.1 Full compressed(br) file test failed!"

curl --ipv4 --http1.1 --compressed --dump-header test/ref_header3.out -H "X-PrefCompAlgo: deflate" http://hauck-thomas.de/output.txt -o test/ref_file3.txt
diff -q test/output.txt test/ref_file3.txt && echo "Http 1.1 Full compressed(deflate) file test passed." || echo "Http 1.1 Full compressed(deflate) file test failed!"


# Partial file
curl --ipv4 --http1.1 --range 500-999 --dump-header test/ref_header4.out https://hauck-thomas.de/output.bin -o test/ref_file4.out
diff -q test/ref_499.bin test/ref_file4.out && echo "Http 1.1 partial file test passed." || echo "Http 1.1 partial file test failed!"

curl --ipv4 --http1.1 --range 50000-60000 --compressed --dump-header test/ref_header5.out https://hauck-thomas.de/output.txt -o test/ref_file5.txt
diff -q test/ref_10000.txt test/ref_file5.txt && echo "Http 1.1 partial compressed(br) file test passed." || echo "Http 1.1 partial compressed(br) file test failed!"

curl --ipv4 --http1.1 --range 50000-60000 --compressed --dump-header test/ref_header6.out -H "X-PrefCompAlgo: deflate" https://hauck-thomas.de/output.txt -o test/ref_file6.txt
diff -q test/ref_10000.txt test/ref_file6.txt && echo "Http 1.1 partial compressed(deflate) file test passed." || echo "Http 1.1 partial compressed(deflate) file test failed!"

# http2 tests
# Full file
curl --ipv4 --http2 --dump-header test/ref_header7.out https://hauck-thomas.de/output.bin -o test/ref_file7.out
diff -q test/output.bin test/ref_file7.out && echo "Http2 Full file test passed." || echo "Http2 Full file test failed!"

curl --ipv4 --http2 --compressed --dump-header test/ref_header8.out https://hauck-thomas.de/output.txt -o test/ref_file8.txt
diff -q test/output.txt test/ref_file8.txt && echo "Http2 Full compressed(br) file test passed." || echo "Http2 Full compressed(br) file test failed!"

curl --ipv4 --http2 --compressed --dump-header test/ref_header9.out -H "X-PrefCompAlgo: deflate" https://hauck-thomas.de/output.txt -o test/ref_file9.txt
diff -q test/output.txt test/ref_file9.txt && echo "Http2 Full compressed(deflate) file test passed." || echo "Http2 Full compressed(deflate) file test failed!"

# Partial file
curl --ipv4 --http2 --range 500-999 --dump-header test/ref_header10.out https://hauck-thomas.de/output.bin -o test/ref_file10.out
diff -q test/ref_499.bin test/ref_file10.out && echo "Http2 partial file test passed." || echo "Http2 partial file test failed!"

curl --ipv4 --http2 --range 50000- --compressed --dump-header test/ref_header11.out https://hauck-thomas.de/output.txt -o test/ref_file11.txt
diff -q test/ref_73456.txt test/ref_file11.txt && echo "Http2 partial compressed(br) file test passed." || echo "Http2 partial compressed(br) file test failed!"

curl --ipv4 --http2 --range 50000- --compressed --dump-header test/ref_header12.out -H "X-PrefCompAlgo: deflate" https://hauck-thomas.de/output.txt -o test/ref_file12.txt
diff -q test/ref_73456.txt test/ref_file12.txt && echo "Http2 partial compressed(deflate) file test passed." || echo "Http2 partial compressed(deflate) file test failed!"
