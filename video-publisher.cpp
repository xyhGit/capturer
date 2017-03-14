#include <arpa/inet.h>

#include "video-publisher.h"
#include "name-components.h"
#include "namespacer.h"
#include "glogger.h"
//#include "utils.h"
#include "mtndn-utils.h"
#include <thread>

//using namespace std::placeholders;
using namespace func_lib;

#define REQUEST_FIRST_FRAME_ 0

/*
VideoPublisher::VideoPublisher (const string streamName,
                      boost::asio::io_service& ioService,
                      ptr_lib::shared_ptr<FaceWrapper> face,
                      KeyChain &keyChain, const Name &certificateName) :
    streamPrefix_(streamName),
    ioService_(ioService),
    face_(face),
    keyChain_ ( keyChain ),
    certificateName_ ( certificateName ),
    registedId_(0),
    responseCount_ ( 0 ),
    requestCounter_(0),
    fp_yuv(NULL),
    fp_264(NULL),
    isBackupYUV(false),
    isBackup264(false),
    cacheSize_(200),
    cachedBegin_(0),
    cachedEnd_(0),
    currentFrameNo_(0)

{
    if( isBackupYUV )
    {
        fp_yuv = fopen ( "backup.yuv", "wb+" );
        if ( fp_yuv == NULL )
            LOG(INFO) << "Open file backup.yuv error!" << endl;
    }
    if( isBackup264 )
    {
        fp_264 = fopen ( "backup.264", "wb+" );
        if ( fp_264 == NULL )
            LOG(INFO) << "Open file backup.264 error!" << endl;
    }

    LOG(INFO) << "VideoPublisher Constructor DONE." << endl;
}
*/

VideoPublisher::VideoPublisher( const GeneralParams &generalParams,
                                IExternalCapturer* const externalCapturer):
    Publisher(generalParams),
    videoCapturer_(new VideoCapturer(externalCapturer)),
    videoEncoder_(new VideoEncoder)
{
    if( isBackupYUV )
    {
        fp_yuv = fopen ( "backup.yuv", "wb+" );
        if ( fp_yuv == NULL )
            LOG(INFO) << "Open file backup.yuv error!" << endl;
    }
    if( isBackup264 )
    {
        fp_264 = fopen ( "backup.264", "wb+" );
        if ( fp_264 == NULL )
            LOG(INFO) << "Open file backup.264 error!" << endl;
    }

    LOG(INFO) << "VideoPublisher Constructor DONE." << endl;
}


VideoPublisher::~VideoPublisher ()
{
    //if( stat != 0 )
        stop();
    if( isBackup264 && fp_264 )
        fclose ( fp_264 );
    if( isBackupYUV && fp_yuv )
        fclose ( fp_yuv );

    LOG(INFO) << "VideoPublisher Destructor DONE." << endl;
}


int
VideoPublisher::init(const PublisherSettings &settings, const MediaThreadParams* videoThreadParams)
{
    LOG(INFO) << "VideoPublihser init" << std::endl;
    if( RESULT_NOT_OK(Publisher::init(settings,videoThreadParams)) )
        return RESULT_ERR;

    videoThreadParams_ = *(VideoThreadParams*)(videoThreadParams);

    videoCapturer_->init();
    videoCapturer_->registerRawFrameConsumer(videoEncoder_.get());
    videoEncoder_->init(videoThreadParams_.coderParams_);
    videoEncoder_->setEncodedFrameConsumer(this);

    return RESULT_OK;
}

int
VideoPublisher::start()
{
    LOG(INFO) << "[VideoPublisher] Start " << std::endl;

    videoCapturer_->start();

    return RESULT_OK;
}

int
VideoPublisher::stop()
{
    LOG(INFO) << "[VideoPublisher] Stop" << std::endl;
    face_->unregisterPrefix(registedId_);

    if(isBackupYUV)
        fclose(fp_yuv);
    if (isBackup264)
        fclose(fp_264);

    return RESULT_OK;
}

void
VideoPublisher::onEncodedFrameDelivered(vector<uint8_t> &encodedImage,
                                   int64_t captureTimestamp)
{
    //cout << "Get 264 " << " ( size = " << outlen264 << " )" <<endl;
    ++currentFrameNo_;
    frameBuffer_->appendData((const unsigned char*)encodedImage.data(),
                             (const unsigned int)encodedImage.size(),
                             captureTimestamp);

    if( isBackup264 && fp_264 )
    {
        fwrite(encodedImage.data(),1,encodedImage.size(),fp_264);
    }
}



void
VideoPublisher::onInterest( const ptr_lib::shared_ptr<const Name>& prefix,
                            const ptr_lib::shared_ptr<const Interest>& interest,
                            Face& face, uint64_t interestFilterId,
                            const ptr_lib::shared_ptr<const InterestFilter>& filter )
{
    LOG(INFO) << "RCVE " << interest->getName().to_uri() << endl;
    ++requestCounter_;

    if( interest->getInterestLifetimeMilliseconds() <= 4000 )
    {
        processRequest(prefix, interest, face, interestFilterId, filter);
    }
    else
    {
        req_.newRequest(interest);
        if( !req_.isvalid() )
        {
            req_.setvalide(true);
            LOG(INFO) << "Req: " << requestCounter_ << " Streaming is restarted" << endl;
            MtNdnUtils::post(
                        [&]()->void{
                            processRequest1(prefix, interest, face, interestFilterId, filter);
                        });
            LOG(INFO) << "Req- " << requestCounter_ << " Streaming is restarted" << endl;
        }
        else
            LOG(INFO) << "Req: " << requestCounter_ << " Streaming is refreshed" << endl;
    }
}

//protected functions
//**************************************************

void
VideoPublisher::processRequest(const ptr_lib::shared_ptr<const Name>& prefix,
                const ptr_lib::shared_ptr<const Interest>& interest, Face& face,
                uint64_t interestFilterId,
                const ptr_lib::shared_ptr<const InterestFilter>& filter)
{
    //cout << "Got an interest..." << endl;

    std::string streamPrefix_ = getStreamName();
    Name requestName(interest->getName());
    Name responseName(streamPrefix_);
    responseName.append(requestName.getSubName(responseName.size()));

    LOG(INFO) << "Request : " << requestName.toUri() << endl;

    ndn::Name metaName(streamPrefix_);
    metaName.append(NameComponents::NameComponentStreamMetaIdx);

    // request metaInfo
    //if( requestName.getPrefix(metaName.size()).equals(metaName))
    if( 0 <= Namespacer::findComponent(requestName,NameComponents::NameComponentStreamMetaIdx))
    {
        Data data(requestName.append(MtNdnUtils::componentFromInt(frameBuffer_->getLastPktNo())));
        face.putData(data);
        ++responseCount_;

        LOG(INFO) << "Response: MetaInfo " << requestName.toUri() << endl << endl;
        return ;
    }

    // data not exist
    ptr_lib::shared_ptr<DataBlock> naluData;
    ndn::Name nalType;
    //naluData = frameBuffer_->acquireData( *interest.get(), nalType );
    naluData = frameBuffer_->acquireData( responseName, nalType );

    FrameNumber firstPktNo, lastPktNo;
    frameBuffer_->getCachedRange(firstPktNo, lastPktNo);
    if( !naluData )
    {
        cout << "No Data: " << interest->getName().toUri()
             << " Cached frameNo: " << firstPktNo << " to " << lastPktNo << endl;
        //Data emptyData(requestName);
        //face.putData(emptyData);
        return ;
    }

    const Blob content ( naluData->dataPtr(), naluData->size() );

    requestName.append(NameComponents::NameComponentNalIdx);
    requestName.append(nalType);
    requestName.append(MtNdnUtils::componentFromInt(lastPktNo));
    //requestName.append(MtNdnUtils::componentFromInt(lastPktNo));
    Data ndnData(requestName);

    ndnData.setContent( content );

    face_->getFace()->getCommandKeyChain()->sign(ndnData,
                                                 face_->getFace()->getCommandCertificateName());


    face.putData(ndnData);
    ++responseCount_;

    LOG(INFO) << "Response: " << requestName.toUri()
         << " ( size: " << dec << ndnData.getContent().size() << " )"
         << endl << endl;
}

void
VideoPublisher::processRequest1(const ptr_lib::shared_ptr<const Name>& prefix,
                                const ptr_lib::shared_ptr<const Interest>& interest,
                                Face& face, uint64_t interestFilterId,
                                const ptr_lib::shared_ptr<const InterestFilter>& filter)
{
    std::string streamPrefix_ = getStreamName();
    //cout << "Got an interest..." << endl;
    cout << streamPrefix_<<endl;
    Name requestName = interest->getName();
    //responseName.append(requestName.getSubName(responseName.size()));
    cout << requestName<<endl;
    ndn::Name metaName(streamPrefix_);
    cout << metaName<<endl;
    metaName.append(NameComponents::NameComponentStreamMetaIdx);
    cout << metaName<<endl;
    ///////////////////////////////////////////////////////////////////////////////////////////
    /// request metaInfo
    ///////////////////////////////////////////////////////////////////////////////////////////
    //if( requestName.getPrefix(metaName.size()).equals(metaName))
    if( 0 <= Namespacer::findComponent(requestName,NameComponents::NameComponentStreamMetaIdx))
    {
        Data data(requestName.append(MtNdnUtils::componentFromInt(frameBuffer_->getLastPktNo())));
        face.putData(data);
        ++responseCount_;

        LOG(INFO) << "Response: MetaInfo " << requestName.toUri() << endl << endl;
        return ;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////
    /// request nalu data
    ///////////////////////////////////////////////////////////////////////////////////////////

    FrameNumber firstPktNo, lastPktNo, counter;
    frameBuffer_->getCachedRange(firstPktNo, counter);

    int64_t currMsTs = MtNdnUtils::millisecSinceEpoch();
    while( req_.isvalid() && currMsTs <= req_.getFinishTsMs() )
    {
        //cout << counter << endl;
        cout << streamPrefix_<<endl;
        Name responseName(streamPrefix_);
        responseName.append(MtNdnUtils::componentFromInt(counter++));
        // data not exist
        ptr_lib::shared_ptr<DataBlock> naluData;
        cout << "streamPrefix_1"<<endl;
        ndn::Name nalType;
        cout << "streamPrefix_2"<<endl;
        //naluData = frameBuffer_->acquireData( *interest.get(), nalType );
        naluData = frameBuffer_->acquireData( responseName, nalType );

        if( !naluData )
        {
            frameBuffer_->getCachedRange(firstPktNo, lastPktNo);
            //cout << "No Data: " << responseName.toUri()
            //     << " Cached frameNo: " << firstPktNo << " to " << lastPktNo << endl;
            //Data emptyData(requestName);
            //face.putData(emptyData);
            --counter;
            usleep(25*1000);
            continue ;
        }

        //got data
        const Blob content ( naluData->dataPtr(), naluData->size() );

        responseName.append(NameComponents::NameComponentNalIdx);
        responseName.append(nalType);
        responseName.append(MtNdnUtils::componentFromInt(lastPktNo));
        //requestName.append(MtNdnUtils::componentFromInt(lastPktNo));
        Data ndnData(responseName);

        ndnData.setContent( content );

        face_->getFace()->getCommandKeyChain()->sign(ndnData,
                                                     face_->getFace()->getCommandCertificateName());

        face.putData(ndnData);
        ++responseCount_;
/*
        LOG(INFO) << "Response: " << responseName.toUri()
             << " ( size: " << dec << ndnData.getContent().size() << " )"
             << endl << endl;
*/
        usleep(25*1000);
        currMsTs = MtNdnUtils::millisecSinceEpoch();
    }
    req_.setvalide(false);

    LOG(INFO) << "Streaming stopped" << endl;
}
