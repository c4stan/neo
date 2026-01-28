import os

BINDINGS = {}

def get(name, default = None):
    global BINDINGS
    if name in BINDINGS:
        return BINDINGS[name]
    else:
        return default

def read(path = 'bindings'):
    global BINDINGS
    BINDINGS = {}
    if os.path.exists(path):
        with open(path) as file:
            for line in file:
                exp = line.split('=')
                name = exp[0].strip()
                value = exp[1].strip()
                BINDINGS[name] = value
    else:
        return False
    return True
