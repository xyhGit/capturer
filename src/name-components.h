#ifndef NAMECOMPONENTS_H
#define NAMECOMPONENTS_H

#include <string>

// Name:  .../monitor/<location>/<streams>/video(audio)/<pktId>/NALU/<naluType>
// Name:  .../video/<streams>/<pktId>/NALU/<naluType>

class NameComponents {
public:
    //static const std::string NameComponentGlobal;
    static const std::string NameComponentApp;
    static const std::string NameComponentStreamFrameVideo;
    static const std::string NameComponentStreamFrameAudio;
    static const std::string NameComponentStreamMetaIdx;
    static const std::string NameComponentNalIdx;
    static const std::string NameComponentExit;
//    static const std::string KeyComponent;
//    static const std::string CertificateComponent;

    static std::string
    getLocationPrefix(const std::string& location,
                  const std::string& prefix);

    static std::string
    getStreamPrefix(const std::string& streamName,
                    const std::string& location,
                    const std::string& prefix);

    static std::string
    getStreamVideoPrefix(const std::string& streamName,
                    const std::string& location,
                    const std::string& prefix);

    static std::string
    getStreamAudioPrefix(const std::string& streamName,
                    const std::string& location,
                    const std::string& prefix);

};

#endif // NAMECOMPONENTS_H
