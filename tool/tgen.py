import sublime
import sublime_plugin

# Deployed in %APPDATA%\Sublime Text\Packages\User

'''
/* template begin

def<T, P, N> $T add_$P ( $T* a, $T* b ) {
    $FOR 0 $N
    // $i
    $END_FOR
    $IF $N > 1
    // 3
    $END_IF
    return *a + *b;
}

make <int, int, 2>

make <float, float, 2>
*/
// template generation begin
int add_int ( int* a, int* b ) {
    // 0
    // 1
    // 3
    return *a + *b;
}

float add_float ( float* a, float* b ) {
    // 0
    // 1
    // 3
    return *a + *b;
}
// template generation end
'''

'''
/* template begin

def <TYPE, PREFIX, SIZE>
typedef union {
    float e[$SIZE];
    struct {
        $TYPE x;
$IF $SIZE > 1
        $TYPE y;
$END_IF
    };
} sm_vec_$SIZE$PREFIX_t;

make <float, f, 3>

*/
// template generation begin
typedef union {
    float e[3];
    struct {
        float x;
        float y;
    };
} sm_vec_3f_t;
// template generation end
'''

'''
/* template begin

def <TYPE, PREFIX, SIZE>
$IF $SIZE > 1
    $TYPE x;
$END_IF
make <float, f, 3>
make <float, f, 4>

*/
// template generation begin

    float x;

    float x;
// template generation end
'''

def generate_template(body):
    original_body = body

    # TODO Remove this token? not needed for anything
    def_token = 'def'
    body = body.lstrip()
    if not body.startswith(def_token):
        return ''

    body = body[len(def_token):].lstrip()

    if not body.startswith('<'):
        return ''

    param_end = body.find('>')
    param_string = body[1:param_end]
    param_list = param_string.split(',')
    param_list = [i.strip() for i in param_list]

    leading_space = 0
    # If there's more than 1 whitespace after <> maintain the spacing to allow custom formatting
    if body[param_end + 1:param_end + 2] == ' ' and body[param_end + 2:param_end + 3] == ' ':
        leading_space = original_body.find('>')
    elif body[param_end + 1:param_end + 2] in ['\n', ' ']:
        param_end = param_end + 1
    body = body[param_end + 1:] #.lstrip()

    make_begin = body.find('make')
    if make_begin == -1:
        return ''

    # separate template body from make invocations
    template = body[:make_begin].rstrip()
    makes = body[make_begin:].rstrip()

    output = ''

    # iterate over make invocations
    trailing_newlines = 0
    make_begin = 0
    while make_begin != -1:
        # find make<> token
        make_end = makes.find('\n', make_begin)
        if make_end == -1:
            make_end = len(makes)
        make = makes[make_begin:make_end]

        make = make[4:].lstrip()
        assert make.startswith('<')

        # extract template args list
        arg_end = make.find('>')
        arg_string = make[1:arg_end]
        arg_list = arg_string.split(',')
        arg_list = [s.strip() for s in arg_list]

        # begin code generation...
        generated_code = template

        # resolve $FOR tokens
        for_begin_token = '$FOR'
        for_end_token = '$END_FOR'
        for_begin = generated_code.find(for_begin_token)
        while for_begin != -1:
            for_args_end = generated_code.find('\n', for_begin)
            for_args = generated_code[for_begin : for_args_end].split(' ')
            for_end = generated_code.find(for_end_token, for_begin)
            for_body = generated_code[for_args_end : for_end].rstrip()

            assert for_end != -1

            # generate loop body, replace $i with loop index
            generated_body = ''
            i = int(for_args[1])
            for_condition = for_args[2]
            if for_condition[0] == '$':
                idx = param_list.index(for_condition[1:])
                for_condition = arg_list[idx]
            while i < int(for_condition):
                generated_body += for_body.replace('$i', str(i))
                i = i + 1

            generated_code = generated_code[:for_begin].rstrip() + generated_body + generated_code[for_end + len(for_end_token):]

            for_begin = generated_code.find(for_begin_token)

        # resolve $IF tokens
        if_begin_token = '$IF'
        if_end_token = '$END_IF'
        if_begin = generated_code.find(if_begin_token)
        while if_begin != -1:
            if_args_end = generated_code.find('\n', if_begin)
            if_args = generated_code[if_begin : if_args_end].split(' ')
            if_end = generated_code.find(if_end_token, if_begin)
            if_body = generated_code[if_args_end+1 : if_end].rstrip()

            print(generated_code[if_begin:if_end])

            assert if_end != -1

            # evaluate condition
            ops = [ '<',           '>',           '<=',             '>=',            '==',            '!='           ]
            evs = [lambda a,b:a<b, lambda a,b:a>b, lambda a,b:a<=b, lambda a,b:a>=b, lambda a,b:a==b, lambda a,b:a!=b]
            lhs = if_args[1]
            if lhs[0] == '$':
                idx = param_list.index(lhs[1:])
                lhs = arg_list[idx]
            rhs = if_args[3]
            if rhs[0] == '$':
                idx = param_list.index(rhs[1:])
                rhs = arg_list[idx]
            if_op = if_args[2]
            op_idx = ops.index(if_op)
            op_result = evs[op_idx](lhs, rhs)

            # generate if body
            generated_body = ''
            if op_result:
                generated_body = if_body

            generated_code = generated_code[:if_begin].rstrip() + generated_body + generated_code[if_end + len(if_end_token):]

            if_begin = generated_code.find(if_begin_token)

        # replace $ tokens in the template with the tokens coming from the make args list
        assert len(arg_list) == len(param_list)
        for param, arg in zip(param_list, arg_list):
            generated_code = generated_code.replace('$' + param.strip(), arg.strip())

        # maintain the spacing between makes
        make_begin = makes.find('make', make_end)
        if make_begin != -1:
            trailing_newlines = max(makes.count('\n', make_end, make_begin) - 1, 0)
        else:
            trailing_newlines = 0

        # append resulting gnenerated code to final output
        if output:
            output = output + '\n'
        output = output + ' ' * leading_space + generated_code + '\n' * trailing_newlines

    return output

class Tgen(sublime_plugin.TextCommand):
    def run(self, edit):
        buffer = self.view.substr(sublime.Region(0, self.view.size()))
        cursor = self.view.sel()[0].begin()

        # TODO make sure template begin is inside a comment block
        # TODO support branching
        # TODO maintain cursor position when generating

        begin_token = 'template begin'
        end_token = '*/'

        appends = []

        start = 0
        begin = buffer.find(begin_token, start)
        while begin != -1:
            end = buffer.find(end_token, begin)
            # if there's already some generated code for the template, cut it out
            if buffer[end + len(end_token):].startswith('\n// template generation begin'):
                gen_end = buffer.find('// template generation end')
                if gen_end != -1:
                    buffer = buffer[:end + len(end_token)] + buffer[gen_end + len('// template generation end'):]
            generated_code = generate_template(buffer[begin + len(begin_token) + 1 : end - 1])
            if generated_code:
                appends.append((end + len(end_token), generated_code))
            start = end
            begin = buffer.find(begin_token, start)

        offset = 0
        for append in appends:
            insert = append[0] + offset
            output = '\n// template generation begin\n' + append[1] + '\n// template generation end'
            buffer = buffer[:insert] + output + buffer[insert:]
            offset = offset + len(output)

        #print(buffer)

        self.view.erase(edit, sublime.Region(0, self.view.size()))
        self.view.insert(edit, 0, buffer)

        self.view.sel().clear()
        self.view.sel().add(sublime.Region(cursor))
