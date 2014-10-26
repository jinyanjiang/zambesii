
#define UDI_VERSION		0x101
#include <udi.h>
#undef UDI_VERSION
#include <__kstdlib/callback.h>
#include <__kstdlib/__kcxxlib/memory>
#include <kernel/common/thread.h>
#include <kernel/common/messageStream.h>
#include <kernel/common/zasyncStream.h>
#include <kernel/common/floodplainn/zum.h>
#include <kernel/common/floodplainn/floodplainn.h>
#include <kernel/common/floodplainn/initInfo.h>
#include <kernel/common/floodplainn/floodplainnStream.h>
#include <kernel/common/cpuTrib/cpuTrib.h>
#include <kernel/common/taskTrib/taskTrib.h>


/**	EXPLANATION:
 * Like the ZUI server, the ZUM server will also take commands over a ZAsync
 * Stream connection.
 *
 * This thread essentially implements all the logic required to allow the kernel
 * to call into a device on its udi_mgmt channel. It doesn't care about any
 * other channels.
 **/

udi_ops_vector_t		dummyMgmtMaOpsVector=0;

namespace zumServer
{
	void zasyncHandler(
		ZAsyncStream::sZAsyncMsg *msg,
		fplainn::Zum::sZAsyncMsg *request,
		Thread *self);

	// Reduces code duplication and increases readability.
	fplainn::Zum::sZumMsg *getNewZumMsg(
		utf8Char *funcName, utf8Char *devicePath,
		processId_t targetPid, ubit16 subsystem, ubit16 function,
		uarch_t size, uarch_t flags, void *privateData)
	{
		fplainn::Zum::sZumMsg		*ret;

		ret = new fplainn::Zum::sZumMsg(
			targetPid, subsystem, function,
			size, flags, privateData);

		if (ret == NULL)
		{
			printf(ERROR ZUM"%s %s: Failed to alloc async ctxt.\n"
				"\tCaller may be halted indefinitely.\n",
				funcName, devicePath);
		}

		return ret;
	}

	namespace start
	{
		void startDeviceReq(
			ZAsyncStream::sZAsyncMsg *msg,
			fplainn::Zum::sZAsyncMsg *request,
			Thread *self);

	// PRIVATE:
		class StartDeviceReqCb;
		typedef void (startDeviceReqCbFn)(
			MessageStream::sHeader *msg,
			fplainn::Zum::sZumMsg *ctxt,
			Thread *self,
			fplainn::Device *dev);

		startDeviceReqCbFn	startDeviceReq1;
	}

	namespace mgmt
	{
		void usageInd(
			ZAsyncStream::sZAsyncMsg *msg,
			fplainn::Zum::sZAsyncMsg *request,
			Thread *self);

		void channelEventInd(
			ZAsyncStream::sZAsyncMsg *msg,
			fplainn::Zum::sZAsyncMsg *request,
			Thread *self);

	// PRIVATE:
		typedef start::StartDeviceReqCb		MgmtReqCb;
		typedef start::startDeviceReqCbFn	mgmtReqCbFn;

		mgmtReqCbFn			usageRes;
		mgmtReqCbFn			enumerateAck;
		mgmtReqCbFn			deviceManagementAck;
		mgmtReqCbFn			finalCleanupAck;
		mgmtReqCbFn			channelEventComplete;

		// Reduces code duplication and improves readability.
		static error_t getDeviceHandleAndMgmtEndpoint(
			utf8Char *funcName,
			utf8Char *devicePath,
			fplainn::Device **retdev, fplainn::Endpoint **retendp)
		{
			error_t		ret;

			ret = floodplainn.getDevice(devicePath, retdev);
			if (ret != ERROR_SUCCESS)
			{
				printf(ERROR ZUM"%s %s: Invalid device name.\n",
					funcName, devicePath);

				return ret;
			};

			*retendp = (*retdev)->instance->mgmtEndpoint;
			if (*retendp == NULL)
			{
				printf(ERROR ZUM"%s %s: Not connected to "
					"device. Try startDeviceReq first.\n",
					funcName, devicePath);

				return ERROR_INVALID_STATE;
			};

			return ERROR_SUCCESS;
		}

		namespace layouts
		{
			udi_layout_t		channel_event_cb[] =
			{
				UDI_DL_UBIT8_T,
				  /* Union. We just instruct env to
				   * copy max bytes.
				   **/
				  UDI_DL_INLINE_UNTYPED,
				  UDI_DL_UBIT8_T,
				  UDI_DL_INLINE_UNTYPED,
				UDI_DL_END
			};

			udi_layout_t		*visible[] =
			{
				channel_event_cb,
				NULL, NULL, NULL, NULL
			};

			udi_layout_t		channel_event_ind[] =
				{ UDI_DL_END };

			udi_layout_t		*marshal[] =
			{
				channel_event_ind,
				NULL, NULL, NULL, NULL
			};
		}
	}


}

void fplainn::Zum::main(void *)
{
	Thread				*self;
	MessageStream::sHeader		*iMsg;
	HeapObj<sZAsyncMsg>		requestData;
	const sMetaInitEntry		*mgmtInitEntry;

	self = cpuTrib.getCurrentCpuStream()->taskStream.getCurrentThread();

	requestData = new sZAsyncMsg(NULL, 0);
	if (requestData.get() == NULL)
	{
		printf(ERROR ZUM"main: failed to alloc request data mem.\n");
		taskTrib.kill(self);
		return;
	};

	mgmtInitEntry = floodplainn.zudi.findMetaInitInfo(CC"udi_mgmt");
	if (mgmtInitEntry == NULL)
	{
		printf(FATAL ZUM"main: failed to get udi_mgmt meta_init.\n");
		taskTrib.kill(self);
		return;
	};

	fplainn::MetaInit	metaInitParser(mgmtInitEntry->udi_meta_info);

	// Construct the fast lookup arrays of visible and marshal layouts.
	for (uarch_t i=1; i<=4; i++)
	{
		udi_mei_op_template_t		*opTemplate;

		opTemplate = metaInitParser.getOpTemplate(UDI_MGMT_OPS_NUM, i);
		if (opTemplate == NULL)
		{
			printf(FATAL ZUM"main: failed to get udi_mgmt op "
				"template for ops_idx %d.\n",
				i);

			taskTrib.kill(self);
			return;
		};

		zumServer::mgmt::layouts::visible[i] =
			opTemplate->visible_layout;

		zumServer::mgmt::layouts::marshal[i] =
			opTemplate->marshal_layout;
	};

	printf(NOTICE ZUM"main: running, tid 0x%x.\n", self->getFullId());

	for (;FOREVER;)
	{
		ZAsyncStream::sZAsyncMsg	*zAMsg;

		self->messageStream.pull(&iMsg);
		switch (iMsg->subsystem)
		{
		case MSGSTREAM_SUBSYSTEM_ZASYNC:
			zAMsg = (ZAsyncStream::sZAsyncMsg *)iMsg;

			zumServer::zasyncHandler(zAMsg, requestData.get(), self);
			break;

		default:
			Callback	*callback;

			callback = (Callback *)iMsg->privateData;
			if (callback == NULL)
			{
				printf(WARNING ZUM"main: message with no "
					"callback from 0x%x.\n",
					iMsg->sourceId);

				continue;
			};

			(*callback)(iMsg);
			delete callback;
		};
	};

	taskTrib.kill(self);
}

void zumServer::zasyncHandler(
	ZAsyncStream::sZAsyncMsg *msg,
	fplainn::Zum::sZAsyncMsg *request,
	Thread *self
	)
{
	error_t			err;

	if (msg->dataNBytes != sizeof(*request))
	{
		printf(WARNING ZUM"incoming request from 0x%x has odd size. "
			"Rejected.\n",
			msg->header.sourceId);

		return;
	};

	err = self->parent->zasyncStream.receive(
		msg->dataHandle, request, 0);

	if (err != ERROR_SUCCESS)
	{
		printf(WARNING ZUM"receive() failed on request from 0x%x.\n",
			msg->header.sourceId);

		return;
	};

	switch (request->opsIndex)
	{
	case fplainn::Zum::sZAsyncMsg::OP_USAGE_IND:
		mgmt::usageInd(msg, request, self);
		break;
	case fplainn::Zum::sZAsyncMsg::OP_ENUMERATE_REQ:
		break;
	case fplainn::Zum::sZAsyncMsg::OP_DEVMGMT_REQ:
		break;
	case fplainn::Zum::sZAsyncMsg::OP_FINAL_CLEANUP_REQ:
		break;
	case fplainn::Zum::sZAsyncMsg::OP_CHANNEL_EVENT_IND:
		mgmt::channelEventInd(msg, request, self);
		break;
	case fplainn::Zum::sZAsyncMsg::OP_START_REQ:
		start::startDeviceReq(msg, request, self);
		break;

	default:
		printf(WARNING ZUM"request from 0x% is for invalid ops_idx "
			"into udi_mgmt_ops_vector_t for device %s.\n",
			msg->header.sourceId,
			request->path);
	};
}

class zumServer::start::StartDeviceReqCb
: public _Callback<startDeviceReqCbFn>
{
	fplainn::Zum::sZumMsg *ctxt;
	Thread *self;
	fplainn::Device *dev;

public:
	StartDeviceReqCb(
		startDeviceReqCbFn *fn,
		fplainn::Zum::sZumMsg *ctxt, Thread *self, fplainn::Device *dev)
	: _Callback<startDeviceReqCbFn>(fn),
	ctxt(ctxt), self(self), dev(dev)
	{}

	virtual void operator()(MessageStream::sHeader *iMsg)
		{ function(iMsg, ctxt, self, dev); }
};

void zumServer::start::startDeviceReq(
	ZAsyncStream::sZAsyncMsg *msg,
	fplainn::Zum::sZAsyncMsg *request,
	Thread *self
	)
{
	fplainn::Zum::sZumMsg		*ctxt;
	error_t				err;
	fplainn::Endpoint		*endp;
	AsyncResponse			myResponse;
	fplainn::Device			*dev;

	ctxt = getNewZumMsg(
		CC __func__, request->path,
		msg->header.sourceId,
		MSGSTREAM_SUBSYSTEM_ZUM, request->opsIndex,
		sizeof(*ctxt), msg->header.flags, msg->header.privateData);

	if (ctxt == NULL) { return; };
	// Copy the request data over.
	new (&ctxt->info) fplainn::Zum::sZAsyncMsg(*request);

	myResponse(ctxt);

	// Does the requested device even exist?
	err = floodplainn.getDevice(ctxt->info.path, &dev);
	if (err != ERROR_SUCCESS) {
		myResponse(err); return;
	};

	/* First thing is to connect() to the device. We store the handle to
	 * our endpoint of the udi_mgmt channel to every device within its
	 * fplainn::DeviceInstance object.
	 *
	 * We can make sure that we haven't already connect()ed to the device by
	 * checking for it first before proceeding.
	 **/
	// Can't think of anything meaningful to use as the endpoint privdata.
	err = self->parent->floodplainnStream.connect(
		ctxt->info.path, CC"udi_mgmt",
		&dummyMgmtMaOpsVector, NULL, 0,
		&endp);

	if (err != ERROR_SUCCESS) {
		myResponse(err); return;
	};

	dev->instance->setMgmtEndpoint(
		static_cast<fplainn::FStreamEndpoint *>(endp));

	/* Now we have our connection. Start sending the initialization
	 * sequence (UDI Core Specification, section 24.2.1):
	 *	udi_usage_ind()
	 *	for (EACH; INTERNAL; BIND; CHILD; ENDPOINT) {
	 *		udi_channel_event_ind(UDI_CHANNEL_BOUND);
	 *	};
	 *	udi_channel_event_ind(parent_bind_channel, UDI_CHANNEL_BOUND);
	 *
	 * And that's it, friends. We'd have at that point, a running UDI
	 * driver, or as the spec describes it, a driver that is "open for
	 * business".
	 **/
	floodplainn.zum.usageInd(
		ctxt->info.path, ctxt->info.params.usage.resource_level,
		new StartDeviceReqCb(startDeviceReq1, ctxt, self, dev));

	myResponse(DONT_SEND_RESPONSE);
}

void zumServer::start::startDeviceReq1(
	MessageStream::sHeader *msg,
	fplainn::Zum::sZumMsg *ctxt,
	Thread *self,
	fplainn::Device *dev
	)
{
	AsyncResponse			myResponse;
	fplainn::Zum::sZumMsg		*response;

	response = (fplainn::Zum::sZumMsg *)msg;
	myResponse(ctxt);

	myResponse(response->header.error);
}

void zumServer::mgmt::usageInd(
	ZAsyncStream::sZAsyncMsg *msg,
	fplainn::Zum::sZAsyncMsg *request,
	Thread *self
	)
{
	fplainn::Endpoint		*endp;
	fplainn::Device			*dev;
	error_t				err;
	fplainn::Zum::sZumMsg		*ctxt;
	AsyncResponse			myResponse;
	udi_usage_cb_t			cb;

	/**	EXPLANATION:
	 * Basically:
	 *	* Get the device, if it exists.
	 *	* Ensure that we are connected to the device first.
	 *	* Get the kernel's mgmt endpoint.
	 *	* Then fill out a udi_usage_cb_t control block.
	 *	* Call FloodplainnStream::send():
	 *		* metaName="udi_mgmt", meta_ops_num=1, ops_idx=1.
	 **/
	ctxt = getNewZumMsg(
		CC __func__, request->path,
		msg->header.sourceId,
		MSGSTREAM_SUBSYSTEM_ZUM, request->opsIndex,
		sizeof(*ctxt), msg->header.flags, msg->header.privateData);

	if (ctxt == NULL) { return; };
	new (&ctxt->info) fplainn::Zum::sZAsyncMsg(*request);
	myResponse(ctxt);

	err = getDeviceHandleAndMgmtEndpoint(
		CC __func__, ctxt->info.path, &dev, &endp);

	if (err != ERROR_SUCCESS) {
		myResponse(err); return;
	};

	cb.trace_mask = ctxt->info.params.usage.cb.trace_mask;
	cb.meta_idx = ctxt->info.params.usage.cb.meta_idx;

	udi_layout_t		*layouts[3] =
	{
		layouts::visible[ctxt->info.opsIndex],
		layouts::marshal[ctxt->info.opsIndex],
		NULL
	};

	err = self->parent->floodplainnStream.send(
		endp, &cb.gcb, layouts,
		CC "udi_mgmt", UDI_MGMT_OPS_NUM, ctxt->info.opsIndex,
		new MgmtReqCb(usageRes, ctxt, self, dev),
		ctxt->info.params.usage.resource_level);

	if (err != ERROR_SUCCESS) {
		myResponse(err); return;
	};

	myResponse(DONT_SEND_RESPONSE);
}

void zumServer::mgmt::usageRes(
	MessageStream::sHeader *msg,
	fplainn::Zum::sZumMsg *ctxt,
	Thread *self,
	fplainn::Device *dev
	)
{
}

void zumServer::mgmt::channelEventInd(
	ZAsyncStream::sZAsyncMsg *msg,
	fplainn::Zum::sZAsyncMsg *request,
	Thread *self
	)
{
	fplainn::Endpoint		*endp;
	fplainn::Device			*dev;
	error_t				err;
	fplainn::Zum::sZumMsg		*ctxt;
	AsyncResponse			myResponse;
	udi_channel_event_cb_t		cb;

	/**	EXPLANATION:
	 * Basically:
	 *	* Get the device, if it exists.
	 *	* Ensure that we are connected to the device first.
	 *	* Get the kernel's mgmt endpoint.
	 *	* Then fill out a udi_channel_event_cb_t control block.
	 *	* Call FloodplainnStream::send():
	 *		* metaName="udi_mgmt", meta_ops_num=1, ops_idx=0.
	 **/
	ctxt = getNewZumMsg(
		CC __func__, request->path,
		msg->header.sourceId,
		MSGSTREAM_SUBSYSTEM_ZUM, request->opsIndex,
		sizeof(*ctxt), msg->header.flags, msg->header.privateData);

	if (ctxt == NULL) { return; };
	new (&ctxt->info) fplainn::Zum::sZAsyncMsg(*request);
	myResponse(ctxt);

	err = getDeviceHandleAndMgmtEndpoint(
		CC __func__, ctxt->info.path, &dev, &endp);

	if (err != ERROR_SUCCESS) {
		myResponse(err); return;
	};

	cb.event = ctxt->info.params.channel_event.cb.event;
	memcpy(
		&cb.params, &ctxt->info.params.channel_event.cb.params,
		sizeof(cb.params));

	udi_layout_t		*layouts[3] =
	{
		layouts::visible[ctxt->info.opsIndex],
		layouts::marshal[ctxt->info.opsIndex],
		NULL
	};

	self->parent->floodplainnStream.send(
		endp, &cb.gcb, layouts,
		CC "udi_mgmt", UDI_MGMT_OPS_NUM, ctxt->info.opsIndex,
		new MgmtReqCb(usageRes, ctxt, self, dev));

	if (err != ERROR_SUCCESS) {
		myResponse(err); return;
	};

	myResponse(DONT_SEND_RESPONSE);
}

void zumServer::mgmt::channelEventComplete(
	MessageStream::sHeader *msg,
	fplainn::Zum::sZumMsg *ctxt,
	Thread *self,
	fplainn::Device *dev
	)
{
}
