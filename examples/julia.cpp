#include <CL/cl.h>
#include <GL/glew.h>
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "OpenCLUtil.h"
#include "OpenGLUtil.h"

using namespace std;
using namespace cl;

int gJuliaSetIndex = 0;

typedef struct {
} process_params;

static void glfw_key_callback(GLFWwindow* wind, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE)
            glfwSetWindowShouldClose(wind, GL_TRUE);
        else if (key == GLFW_KEY_1)
            gJuliaSetIndex = 0;
        else if (key == GLFW_KEY_2)
            gJuliaSetIndex = 1;
    }
}

inline unsigned divup(unsigned a, unsigned b) {
    return (a+b-1)/b;
}

int main(void) {
    const float matrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    const float vertices[12] = {
        -1.0f,-1.0f, 0.0,
        1.0f,-1.0f, 0.0,
        1.0f, 1.0f, 0.0,
        -1.0f, 1.0f, 0.0
    };
    const float texcords[8] = {
        0.0, 1.0,
        1.0, 1.0,
        1.0, 0.0,
        0.0, 0.0
    };
    const unsigned int indices[6] = {
        0, 1, 2,
        0, 2, 3
    };
    const float CJULIA[] = {
        -0.700f,  0.270f,
        -0.618f,  0.000f,
        -0.400f,  0.600f,
         0.285f,  0.000f,
         0.285f,  0.010f,
         0.450f,  0.143f,
        -0.702f, -0.384f,
        -0.835f, -0.232f,
        -0.800f,  0.156f,
         0.279f,  0.000f
    };
    int
        wind_width = 720,
        wind_height = 720
    ;
    GLuint prg, vao, tex;
    // CL vars.
    CommandQueue queue;
    Device device;
    ImageGL image_gl;
    Kernel kernel;
    Program program;
    cl::size_t<3> dimensions;
    cl_int errCode;

    // Window.
    if (!glfwInit())
        return 255;
    GLFWwindow* window;
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    window = glfwCreateWindow(wind_width,wind_height,"Julia Sets",NULL,NULL);
    if (!window) {
        glfwTerminate();
        return 254;
    }
    glfwMakeContextCurrent(window);
    GLenum res = glewInit();
    if (res!=GLEW_OK) {
        std::cout<<"Error Initializing GLEW | Exiting"<<std::endl;
        return 253;
    }
    glfwSetKeyCallback(window,glfw_key_callback);

    Platform lPlatform = getPlatform();
    // Select the default platform and create a context using this platform and the GPU
    cl_context_properties cps[] = {
        CL_GL_CONTEXT_KHR, (cl_context_properties)glfwGetGLXContext(window),
        CL_GLX_DISPLAY_KHR, (cl_context_properties)glfwGetX11Display(),
        CL_CONTEXT_PLATFORM, (cl_context_properties)lPlatform(),
        0
    };
    std::vector<Device> devices;
    lPlatform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
    // Get a list of devices on this platform
    for (unsigned d=0; d<devices.size(); ++d) {
        if (checkExtnAvailability(devices[d],CL_GL_SHARING_EXT)) {
            device = devices[d];
            break;
        }
    }
    Context context(device, cps);
    // Create a command queue and use the first device
    queue = CommandQueue(context, device);
    program = getProgram(context, ASSETS_DIR "/fractal.cl",errCode);
    std::ostringstream options;
    options << "-I " << std::string(ASSETS_DIR);
    program.build(std::vector<Device>(1, device), options.str().c_str());
    kernel = Kernel(program, "fractal");
    // create opengl stuff
    prg = initShaders(ASSETS_DIR "/fractal.vert", ASSETS_DIR "/fractal.frag");
    tex = createTexture2D(wind_width,wind_height);

    GLuint vbo = createBuffer(12,vertices,GL_STATIC_DRAW);
    GLuint tbo = createBuffer(8,texcords,GL_STATIC_DRAW);
    GLuint ibo;
    glGenBuffers(1,&ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(unsigned int)*6,indices,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glGenVertexArrays(1,&vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,NULL);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,tbo);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,NULL);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
    glBindVertexArray(0);
    // create opengl texture reference using opengl texture
    image_gl = ImageGL(context,CL_MEM_READ_WRITE,GL_TEXTURE_2D,0,tex,&errCode);
    if (errCode!=CL_SUCCESS) {
        std::cout<<"Failed to create OpenGL texture refrence: "<<errCode<<std::endl;
        return 250;
    }
    dimensions[0] = wind_width;
    dimensions[1] = wind_height;
    dimensions[2] = 1;

    while (!glfwWindowShouldClose(window)) {
        // Time step.
        {
            cl::Event ev;
            try {
                glFinish();

                std::vector<Memory> objs;
                objs.clear();
                objs.push_back(image_gl);
                // flush opengl commands and wait for object acquisition
                cl_int res = queue.enqueueAcquireGLObjects(&objs,NULL,&ev);
                ev.wait();
                if (res!=CL_SUCCESS) {
                    std::cout<<"Failed acquiring GL object: "<<res<<std::endl;
                    exit(248);
                }
                NDRange local(16, 16);
                NDRange global( local[0] * divup(dimensions[0], local[0]),
                                local[1] * divup(dimensions[1], local[1]));
                // set kernel arguments
                kernel.setArg(0, image_gl);
                kernel.setArg(1, (int)dimensions[0]);
                kernel.setArg(2, (int)dimensions[1]);
                kernel.setArg(3, 1.0f);
                kernel.setArg(4, 1.0f);
                kernel.setArg(5, 0.0f);
                kernel.setArg(6, 0.0f);
                kernel.setArg(7, CJULIA[2*gJuliaSetIndex+0]);
                kernel.setArg(8, CJULIA[2*gJuliaSetIndex+1]);
                queue.enqueueNDRangeKernel(kernel,cl::NullRange, global, local);
                // release opengl object
                res = queue.enqueueReleaseGLObjects(&objs);
                ev.wait();
                if (res!=CL_SUCCESS) {
                    std::cout<<"Failed releasing GL object: "<<res<<std::endl;
                    exit(247);
                }
                queue.finish();
            } catch(Error err) {
                std::cout << err.what() << "(" << err.err() << ")" << std::endl;
            }
        }

        // render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearColor(0.2,0.2,0.2,0.0);
        glEnable(GL_DEPTH_TEST);
        glUseProgram(prg);
        int mat_loc = glGetUniformLocation(prg,"matrix");
        int tex_loc = glGetUniformLocation(prg,"tex");
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(tex_loc,0);
        glBindTexture(GL_TEXTURE_2D,tex);
        glGenerateMipmap(GL_TEXTURE_2D);
        glUniformMatrix4fv(mat_loc,1,GL_FALSE,matrix);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup.
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
