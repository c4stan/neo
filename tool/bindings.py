import os

def load(path):
    entries = {}
    if os.path.exists(path):
        with open(path) as file:
            for line in file:
                exp = line.split('=')
                name = exp[0].strip()
                value = exp[1].strip()
                entries[name] = value
    else:
        return None
    return entries
