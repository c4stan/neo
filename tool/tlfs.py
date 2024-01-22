import math

def convert_size(size_bytes):
    return size_bytes
    if size_bytes == 0:
        return "0B"
    size_name = ("B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB")
    i = int(math.floor(math.log(size_bytes, 1024)))
    p = math.pow(1024, i)
    s = round(size_bytes / p, 2)
    return "%s %s" % (s, size_name[i])

min_x = 10
max_x = 36
buckets = 16

for i in range(min_x, max_x - 1):
    base = pow(2, i)
    end = pow(2, i + 1)# - 1

    print("[2^" + str(i) + "] ")#, end="")
    print(convert_size(base))

    bucket = (end - base) / buckets
    for j in range(1, buckets):
        print(convert_size(base + bucket * j))
