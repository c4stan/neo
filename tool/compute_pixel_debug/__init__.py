import qrenderdoc as qrd
import renderdoc as rd

extiface_version = ''

def extension_callback(ctx: qrd.CaptureContext, data):
    ctx.ShowDiagnosticLogView()
    pixel_x, pixel_y = ctx.GetTextureViewer().GetPickedLocation()

    pipe = None
    def get_pipe(r: rd.ReplayController):
        nonlocal pipe
        pipe = r.GetPipelineState()
    ctx.Replay().BlockInvoke(get_pipe)

    thread_group_size = [8, 8]
    thread_group_x = pixel_x // thread_group_size[0]
    thread_group_y = pixel_y // thread_group_size[1]
    local_thread_x = pixel_x % thread_group_size[0]
    local_thread_y = pixel_y % thread_group_size[1]

    print(f"Pixel ({pixel_x}, {pixel_y}) maps to:")
    print(f"  Thread Group: ({thread_group_x}, {thread_group_y}, 0)")
    print(f"  Local Thread: ({local_thread_x}, {local_thread_y}, 0)")

    trace = None
    def get_trace(r: rd.ReplayController):
        nonlocal trace
        trace = r.DebugThread([thread_group_x, thread_group_y, 0], [local_thread_x, local_thread_y, 0])
        print(trace)
    ctx.Replay().BlockInvoke(get_trace)

    resource = pipe.GetComputePipelineObject()
    reflection = pipe.GetShaderReflection(rd.ShaderStage.Compute)
    viewer = ctx.DebugShader(reflection, resource, trace, "")

    ctx.AddDockWindow(viewer.Widget(), qrd.DockReference.MainToolArea, None)
    ctx.RaiseDockWindow(viewer.Widget())


def register(version: str, ctx: qrd.CaptureContext):
    global extiface_version
    extiface_version = version

    print("Registering 'Compute Pixel Debug' extension for RenderDoc version {}".format(version))

    ctx.Extensions().RegisterWindowMenu(qrd.WindowMenu.Tools, ["Compute Pixel Debug"], extension_callback)


def unregister():
    print("Unregistering 'Compute Pixel Debug' extension")
