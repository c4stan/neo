import math

def get_align(size):
    if size % 2 != 0: return 1
    factor = 0
    while size % 2 == 0:
        size /= 2
        factor += 1
    return 2 ** factor

def convert_size_long(size):
    return "{} | {}".format(size, get_align(size))

def format_short_size(size):
    units = ("B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB")
    i = int(math.floor(math.log(size, 1024)))
    p = math.pow(1024, i)
    s = round(size / p, 2)
    return "{} {}".format(s, units[i])

def convert_size_short(size):    
    if size == 0:
        return "0 B"
    size_name = ("B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB")
    i = int(math.floor(math.log(size, 1024)))
    p = math.pow(1024, i)
    s = round(size / p, 2)
    return "{} | {}".format(format_short_size(size), format_short_size(get_align(size)),)

def convert_size(size):
    return convert_size_short(size)

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
