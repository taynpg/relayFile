#include "ClientHelper.h"

ClientWorker::ClientWorker(ClientCore* core, QObject* parent) : QThread(parent), core_(core)
{
}

ClientWorker::~ClientWorker()
{
}

void ClientWorker::run()
{
    core_->instance();
    exec();
}
