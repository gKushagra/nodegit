NAN_METHOD(GitPatch::ConvenientFromDiff) {
  if (info.Length() == 0 || !info[0]->IsObject()) {
    return Nan::ThrowError("Diff diff is required.");
  }

  if (info.Length() == 1 || !info[1]->IsFunction()) {
    return Nan::ThrowError("Callback is required and must be a Function.");
  }

  ConvenientFromDiffBaton *baton = new ConvenientFromDiffBaton();

  baton->error_code = GIT_OK;
  baton->error = NULL;

  baton->diff = Nan::ObjectWrap::Unwrap<GitDiff>(Nan::To<v8::Object>(info[0]).ToLocalChecked())->GetValue();
  baton->out = new std::vector<PatchData *>;
  baton->out->reserve(git_diff_num_deltas(baton->diff));

  Nan::Callback *callback = new Nan::Callback(Local<Function>::Cast(info[1]));
  ConvenientFromDiffWorker *worker = new ConvenientFromDiffWorker(baton, callback);

  worker->SaveToPersistent("diff", info[0]);

  nodegit::Context *nodegitContext = reinterpret_cast<nodegit::Context *>(info.Data().As<External>()->Value());
  nodegitContext->QueueWorker(worker);
  return;
}

nodegit::LockMaster GitPatch::ConvenientFromDiffWorker::AcquireLocks() {
  nodegit::LockMaster lockMaster(true, baton->diff);
  return lockMaster;
}

void GitPatch::ConvenientFromDiffWorker::Execute() {
  git_error_clear();

  std::vector<git_patch *> patchesToBeFreed;

  for (int i = 0; i < git_diff_num_deltas(baton->diff); ++i) {
    git_patch *nextPatch;
    int result = git_patch_from_diff(&nextPatch, baton->diff, i);

    if (result) {
      while (!patchesToBeFreed.empty())
      {
        git_patch_free(patchesToBeFreed.back());
        patchesToBeFreed.pop_back();
      }

      while (!baton->out->empty()) {
        PatchDataFree(baton->out->back());
        baton->out->pop_back();
      }

      baton->error_code = result;

      if (git_error_last() != NULL) {
        baton->error = git_error_dup(git_error_last());
      }

      delete baton->out;
      baton->out = NULL;

      return;
    }

    if (nextPatch != NULL) {
      baton->out->push_back(createFromRaw(nextPatch));
      patchesToBeFreed.push_back(nextPatch);
    }
  }

  while (!patchesToBeFreed.empty())
  {
    git_patch_free(patchesToBeFreed.back());
    patchesToBeFreed.pop_back();
  }
}

void GitPatch::ConvenientFromDiffWorker::HandleErrorCallback() {
  if (baton->error) {
    if (baton->error->message) {
      free((void *)baton->error->message);
    }

    free((void *)baton->error);
  }

  delete baton;
}

void GitPatch::ConvenientFromDiffWorker::HandleOKCallback() {
  if (baton->out != NULL) {
    unsigned int size = baton->out->size();
    Local<Array> result = Nan::New<Array>(size);

    for (unsigned int i = 0; i < size; ++i) {
      Nan::Set(result, Nan::New<Number>(i), ConvenientPatch::New((void *)baton->out->at(i)));
    }

    delete baton->out;

    Local<v8::Value> argv[2] = {
      Nan::Null(),
      result
    };
    callback->Call(2, argv, async_resource);

    return;
  }

  if (baton->error) {
    Local<v8::Object> err;
    if (baton->error->message) {
      err = Nan::To<v8::Object>(Nan::Error(baton->error->message)).ToLocalChecked();
    } else {
      err = Nan::To<v8::Object>(Nan::Error("Method convenientFromDiff has thrown an error.")).ToLocalChecked();
    }
    Nan::Set(err, Nan::New("errno").ToLocalChecked(), Nan::New(baton->error_code));
    Nan::Set(err, Nan::New("errorFunction").ToLocalChecked(), Nan::New("Patch.convenientFromDiff").ToLocalChecked());
    Local<v8::Value> argv[1] = {
      err
    };
    callback->Call(1, argv, async_resource);
    if (baton->error->message)
    {
      free((void *)baton->error->message);
    }

    free((void *)baton->error);

    return;
  }

  if (baton->error_code < 0) {
    Local<v8::Object> err = Nan::To<v8::Object>(Nan::Error("method convenientFromDiff has thrown an error.")).ToLocalChecked();
    Nan::Set(err, Nan::New("errno").ToLocalChecked(), Nan::New(baton->error_code));
    Nan::Set(err, Nan::New("errorFunction").ToLocalChecked(), Nan::New("Patch.convenientFromDiff").ToLocalChecked());
    Local<v8::Value> argv[1] = {
      err
    };
    callback->Call(1, argv, async_resource);

    return;
  }

  Nan::Call(*callback, 0, NULL);
}
