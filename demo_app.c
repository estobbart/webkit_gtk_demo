// gcc demo_app.c -o demo `pkg-config --cflags --libs webkitgtk-3.0`

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <JavaScriptCore/JavaScript.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

// http://www.pcg-random.org/
// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)

typedef struct { uint64_t state;  uint64_t inc; } pcg32_random_t;

uint32_t pcg32_random_r(pcg32_random_t* rng)
{
    uint64_t oldstate = rng->state;
    // Advance internal state
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
    // Calculate output function (XSH RR), uses old state for max ILP
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}



/******************************************************************************
* Struct for for JavaScript Thing object
******************************************************************************/

uint64_t make_llong(pcg32_random_t *rng) {
  return (uint64_t)pcg32_random_r(rng) << 32 | pcg32_random_r(rng);
}

typedef struct {
    char * x;
    uint64_t (*llong_func)();
    pcg32_random_t rng;
} Thing;

/******************************************************************************
* Javascript Extensions for Thing object.
******************************************************************************/

const char * className = "Thing";

JSClassRef thing_class_ref();

void initialize(JSContextRef ctx, JSObjectRef object) {
    Thing *_thing = (Thing *)JSObjectGetPrivate(object);
    if (_thing) {
        g_message("%s has pointer to thing", className);
        _thing->x = NULL;
        _thing->llong_func = make_llong;
        int fd = open("/dev/random", O_RDONLY);
        if (fd < 0) {
          g_message("no device!");
        }
        uint64_t seeds[2];
        int sz = read(fd, (void *)seeds, sizeof(seeds));
        if (close(fd) != 0) {
          g_message("failure to close!");
        }
        _thing->rng = (pcg32_random_t){ seeds[0], seeds[1] };
    }
    g_message("%s initialize", className);
}

void finalize(JSObjectRef object) {
    Thing *_thing = (Thing *)JSObjectGetPrivate(object);
    if (_thing) {
        if (_thing->x) {
          free(_thing->x);
        }
        free(_thing);
    }
    g_message("%s finalize", className);
}

JSObjectRef constructor(JSContextRef ctx, JSObjectRef constructor,
                        size_t argumentCount, const JSValueRef arguments[],
                        JSValueRef* exception) {
    g_message("%s constructor", className);
    Thing *_thing = malloc(sizeof(Thing));
    return JSObjectMake(ctx, thing_class_ref(), _thing);
}

JSValueRef get_x(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef* exception) {
    g_message("Getting: %s", JSStringGetCharactersPtr(propertyName));
    Thing *_thing = (Thing *)JSObjectGetPrivate(object);
    if (_thing) {
        JSStringRef str;
        if (_thing->x) {
            str = JSStringCreateWithUTF8CString(_thing->x);
        } else {
            str = JSStringCreateWithUTF8CString("");
        }
        JSValueRef strRef = JSValueMakeString(ctx, str);
        JSStringRelease(str);
        return strRef;
    }
    // Forward the request up the prototype chain
    //return NULL;
    return JSValueMakeUndefined(ctx);
}

bool set_x(JSContextRef ctx, JSObjectRef object, JSStringRef propertyName, JSValueRef value, JSValueRef* exception) {
    g_message("Setting: %s", JSStringGetCharactersPtr(propertyName));
    Thing *_thing = (Thing *)JSObjectGetPrivate(object);
    if (JSValueIsString(ctx, value)) {
        if (!_thing) {
          g_message("Not a thing");
          return false;
        }
        if (_thing->x) {
          free(_thing->x);
        }
        JSStringRef string = JSValueToStringCopy(ctx, value, exception);
        size_t max = JSStringGetMaximumUTF8CStringSize(string);
        char * buffer = malloc(sizeof(max));
        size_t ret = JSStringGetUTF8CString(string, buffer, max);
        g_message("Set as string: '%s' size:", buffer, ret);
        _thing->x = buffer;
    } else {
        // NOTE(estobbart): Could be converted.. but this is a demo to show type safety.
        JSStringRef errString = JSStringCreateWithUTF8CString("oops, not a string");
        const JSValueRef args[] = { JSValueMakeString(ctx, errString) };
        JSStringRelease(errString);
        *exception = JSObjectMakeError(ctx, 1, args, exception);
    }
    return true;
}


JSStaticValue staticValuesArray[] = {
   { "x", get_x, set_x, kJSPropertyAttributeNone },
   { 0, 0, 0, 0 }
};

JSValueRef makeLong(JSContextRef ctx, JSObjectRef function,
                    JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[],
                    JSValueRef* exception) {
    g_message("Make long with %p", thisObject);
    JSObjectRef con = JSObjectMakeConstructor(ctx, thing_class_ref(), NULL);
    if (!JSValueIsInstanceOfConstructor(ctx, thisObject, con, exception)) {
      // Intention here is to prevent against the following JavaScript...
      // var t = new Thing();
      // var stolenMakeLong = t.makeLong;
      // stolenMakeLong();
      g_message("Sorry... not a Thing object.");
      return JSValueMakeUndefined(ctx);
    }
    Thing *_thing = (Thing *)JSObjectGetPrivate(thisObject);
    if (_thing) {
        char buffer[128];
        int ret = snprintf(buffer, sizeof(buffer), "%ld", _thing->llong_func(&_thing->rng));
        g_message("wrote %i chars", ret);
        JSStringRef str = JSStringCreateWithUTF8CString(buffer);
        JSValueRef result = JSValueMakeString(ctx, str);
        JSStringRelease(str);
        return result;
    }
    return JSValueMakeUndefined(ctx);
}

JSStaticFunction staticFunctionsArray[] = {
   { "makeLong", makeLong, kJSPropertyAttributeNone},
   { 0, 0, 0 }
};

const JSClassDefinition classDef = {
    1,                     // version
    // kJSClassAttributeNoAutomaticPrototype proto = Object
    // kJSClassAttributeNone proto = ConstructorCallback with all staticFuncs
    kJSClassAttributeNone, // attributes
    "[object Thing]",      // className
    NULL,                  // parentClass
    staticValuesArray,     // staticValues
    staticFunctionsArray,  // staticFunctions
    // Anytime JSObjectMake is called with this class definition.
    initialize,            // initialize
    // Anytime the garbage collector kicks in and cleans up your object.
    finalize,              // finalize
    NULL,                  // hasProperty
    NULL,                  // getProperty
    NULL,                  // setProperty
    NULL,                  // deleteProperty
    NULL,                  // getPropertyNames
    NULL,                  // callAsFunction
    // Anytime JSObjectCallAsContstructor, or new CustomClass() is called.
    constructor,           // callAsConstructor
    NULL,                  // hasInstance
    NULL                   // convertToType
};

JSClassRef thing_class_ref() {
    static JSClassRef class = NULL;
    if (!class) {
        class = JSClassCreate(&classDef);
    }
    return class;
}

JSValueRef gc(JSContextRef ctx, JSObjectRef function,
              JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[],
              JSValueRef* exception) {
   JSGarbageCollect(ctx);
   return JSValueMakeUndefined(ctx);
}


/******************************************************************************
* WebKitGTK application
******************************************************************************/

void window_object_cleared(WebKitWebView  *web_view,
                           WebKitWebFrame *frame,
                           gpointer        context,
                           gpointer        window_object,
                           gpointer        user_data) {
    g_message("window object cleared");
    JSObjectRef classObj = JSObjectMake(context, thing_class_ref(), NULL);
    JSObjectRef globalObj = JSContextGetGlobalObject(context);
    JSStringRef str = JSStringCreateWithUTF8CString(className);
    JSObjectSetProperty(context, globalObj, str, classObj, kJSPropertyAttributeNone, NULL);
    JSStringRelease(str);

    // Allows you to test finalize method by forcing the Garbage collector to mark & sweep.
    JSStringRef gcName = JSStringCreateWithUTF8CString("gc");
    JSObjectRef gcFunction = JSObjectMakeFunctionWithCallback(context, gcName, gc);
    JSObjectSetProperty(context, globalObj, gcName, gcFunction, kJSPropertyAttributeNone, NULL);
    JSStringRelease(gcName);
}

WebKitWebView* inspector_created(WebKitWebInspector  *web_inspector,
                                 WebKitWebView       *web_view,
                                 gpointer            user_data) {
    g_message("inspector_created");
    GtkWidget *inspector_view = webkit_web_view_new();
    gtk_paned_add2(GTK_PANED(user_data), inspector_view);
    return WEBKIT_WEB_VIEW(inspector_view);
}

void destroy(GtkWidget *widget, gpointer   data) {
    gtk_main_quit();
}


int main (int argc, char* argv[]) {
    gtk_init (&argc, &argv);

    GtkWidget *main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *split_view = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    GtkWidget *web_view = webkit_web_view_new();
    WebKitWebSettings *webkitSettings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(web_view));
    g_object_set(G_OBJECT(webkitSettings), "enable-developer-extras", TRUE, NULL);

    g_signal_connect(G_OBJECT(web_view), "window-object-cleared",
                     G_CALLBACK(window_object_cleared), web_view);

    gtk_container_add(GTK_CONTAINER(scrolled_window), web_view);
    gtk_paned_add1(GTK_PANED(split_view), scrolled_window);
    gtk_container_add(GTK_CONTAINER(main_window), split_view);

    g_signal_connect(G_OBJECT(main_window), "destroy",
                     G_CALLBACK(destroy), NULL);

    char * url;
    if (argc > 1) {
        url = argv[1];
    } else {
        // NOTE(estobbart): If a page doesn't have a script tag
        // window-object-cleared never fires. If you then open the inspector
        // and type in the console, it will fire. You can see this from the
        // delayed stdout prints in the terminal.
        url = "about:blank";
    }
    g_message("Loading: %s", url);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), url);

    gtk_window_set_default_size(GTK_WINDOW(main_window), 800, 600);

    gtk_widget_show_all(main_window);

    WebKitWebInspector *inspector = webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(web_view));
    g_signal_connect(G_OBJECT(inspector), "inspect-web-view",
                     G_CALLBACK(inspector_created), split_view);
    // Uncomment if you want the inspector automatically at launch
    //webkit_web_inspector_show(inspector);
    gtk_main();
    return 0;
}
