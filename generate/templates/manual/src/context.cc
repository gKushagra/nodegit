#include "../include/context.h"

namespace nodegit {
  std::map<v8::Isolate *, Context *> Context::contexts;

  AsyncContextCleanupHandle::AsyncContextCleanupHandle(v8::Isolate *isolate, Context *context)
    : context(context),
#if IS_CONTEXT_AWARE_NODE_MODULE_VERSION
      handle(node::AddEnvironmentCleanupHook(isolate, AsyncCleanupContext, this))
#else
      handle(nullptr)
#endif
  {}

  AsyncContextCleanupHandle::~AsyncContextCleanupHandle() {
    delete context;
    doneCallback(doneData);
  }

  void AsyncContextCleanupHandle::AsyncCleanupContext(void *data, void(*uvCallback)(void*), void *uvCallbackData) {
    std::unique_ptr<AsyncContextCleanupHandle> cleanupHandle(static_cast<AsyncContextCleanupHandle *>(data));
    cleanupHandle->doneCallback = uvCallback;
    cleanupHandle->doneData = uvCallbackData;
    // the ordering of std::move and the call to Context::ShutdownThreadPool prohibits
    // us from referring to context on cleanupHandle if we're also intending to move
    // the unique_ptr into the method.
    Context *context = cleanupHandle->context;
    context->ShutdownThreadPool(std::move(cleanupHandle));
  }

  Context::Context(v8::Isolate *isolate)
    : isolate(isolate)
    , threadPool(10, node::GetCurrentEventLoop(isolate), this)
  {
    Nan::HandleScope scopoe;
    v8::Local<v8::Object> storage = Nan::New<v8::Object>();
    persistentStorage.Reset(storage);
    contexts[isolate] = this;
    new AsyncContextCleanupHandle(isolate, this);
  }

  Context::~Context() {
    contexts.erase(isolate);
  }

  Context *Context::GetCurrentContext() {
    Nan::HandleScope scope;
    v8::Local<v8::Context> context = Nan::GetCurrentContext();
    v8::Isolate *isolate = context->GetIsolate();
    return contexts[isolate];
  }

  v8::Local<v8::Value> Context::GetFromPersistent(std::string key) {
    Nan::EscapableHandleScope scope;
    v8::Local<v8::Object> storage = Nan::New(persistentStorage);
    Nan::MaybeLocal<v8::Value> value = Nan::Get(storage, Nan::New(key).ToLocalChecked());
    return scope.Escape(value.ToLocalChecked());
  }

  void Context::QueueWorker(nodegit::AsyncWorker *worker) {
    threadPool.QueueWorker(worker);
  }

  void Context::SaveToPersistent(std::string key, const v8::Local<v8::Value> &value) {
    Nan::HandleScope scope;
    v8::Local<v8::Object> storage = Nan::New(persistentStorage);
    Nan::Set(storage, Nan::New(key).ToLocalChecked(), value);
  }

  void Context::ShutdownThreadPool(std::unique_ptr<AsyncContextCleanupHandle> cleanupHandle) {
    threadPool.Shutdown(std::move(cleanupHandle));
  }
}
