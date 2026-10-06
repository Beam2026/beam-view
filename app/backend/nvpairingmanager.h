#pragma once

#include "identitymanager.h"
#include "nvhttp.h"

#include <openssl/x509.h>
#include <openssl/evp.h>

class NvPairingManager
{
public:
    enum PairState
    {
        PAIRED,
        PIN_WRONG,
        FAILED,
        ALREADY_IN_PROGRESS
    };

    explicit NvPairingManager(NvComputer* computer);

    ~NvPairingManager();

    PairState
    pair(QString appVersion, QString pin, QSslCertificate& serverCert);

    // Beam (P10): the Beam session this process pairs for, sent as `beamid=` with every pairing
    // request so the host's Sunshine (beam-share) can match it to the PIN its host approved.
    static void
    setBeamSessionId(const QString& id);

private:
    // The arguments every pairing request starts with.
    static QString
    pairArguments();

    static QString s_BeamSessionId;

    QByteArray
    generateRandomBytes(int length);

    QByteArray
    saltPin(const QByteArray& salt, QString pin);

    QByteArray
    encrypt(const QByteArray& plaintext, const QByteArray& key);

    QByteArray
    decrypt(const QByteArray& ciphertext, const QByteArray& key);

    QByteArray
    getSignatureFromCert(X509* cert);

    QByteArray
    getSignatureFromPemCert(const QByteArray& certificate);

    bool
    verifySignature(const QByteArray& data, const QByteArray& signature, const QByteArray& serverCertificate);

    QByteArray
    signMessage(const QByteArray& message);

    NvHTTP m_Http;
    X509* m_Cert;
    EVP_PKEY* m_PrivateKey;
};
