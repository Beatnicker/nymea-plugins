#include "owletserialtransport.h"
#include "owlettransport.h"
#include "extern-plugininfo.h"

#include <QDataStream>

OwletSerialTransport::OwletSerialTransport(const QString &serialPortName, uint baudrate, QObject *parent) :
    OwletTransport(parent),
    m_serialPortName(serialPortName),
    m_baudrate(baudrate)
{
    qRegisterMetaType<QSerialPort::SerialPortError>();

    m_serialPort = new QSerialPort(this);
    m_serialPort->setPortName(serialPortName);
    m_serialPort->setBaudRate(115200);
    m_serialPort->setDataBits(QSerialPort::DataBits::Data8);
    m_serialPort->setParity(QSerialPort::Parity::NoParity);
    m_serialPort->setStopBits(QSerialPort::StopBits::OneStop);
    m_serialPort->setFlowControl(QSerialPort::FlowControl::NoFlowControl);

    connect(m_serialPort, &QSerialPort::readyRead, this, &OwletSerialTransport::onReadyRead);
    typedef void (QSerialPort:: *errorSignal)(QSerialPort::SerialPortError);
    connect(m_serialPort, static_cast<errorSignal>(&QSerialPort::error), this, [=](){
        if (m_serialPort->error() != QSerialPort::NoError) {
            qCWarning(dcOwlet()) << "Serial port error occured" << m_serialPort->error() << m_serialPort->errorString();
            emit error();
            m_reconnectTimer->start();
            if (m_serialPort->isOpen()) {
                m_serialPort->close();
            }
            emit connectedChanged(false);
        }
    });

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(5000);
    m_reconnectTimer->setSingleShot(false);
    connect(m_reconnectTimer, &QTimer::timeout, this, [=](){
        if (m_serialPort->isOpen()) {
            m_reconnectTimer->stop();
            return;
        } else {
            connectTransport();
        }
    });
}

bool OwletSerialTransport::connected() const
{
    return m_serialPort->isOpen();
}

void OwletSerialTransport::sendData(const QByteArray &data)
{
    // Stream bytes using SLIP
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream << static_cast<quint8>(SlipProtocolEnd);

    for (int i = 0; i < data.length(); i++) {
        quint8 dataByte = data.at(i);
        switch (dataByte) {
        case SlipProtocolEnd:
            stream << static_cast<quint8>(SlipProtocolEsc);
            stream << static_cast<quint8>(SlipProtocolTransposedEnd);
            break;
        case SlipProtocolEsc:
            stream << static_cast<quint8>(SlipProtocolEsc);
            stream << static_cast<quint8>(SlipProtocolTransposedEsc);
            break;
        default:
            stream << dataByte;
            break;
        }
    }
    stream << static_cast<quint8>(SlipProtocolEnd);

    qCDebug(dcOwlet()) << "UART -->" << qUtf8Printable(data) << message.toHex();
    m_serialPort->write(message);
    m_serialPort->flush();
}

void OwletSerialTransport::connectTransport()
{
    if (m_serialPort->isOpen())
        return;

    qCDebug(dcOwlet()) << "Connecting to" << m_serialPortName;
    bool serialPortFound = false;
    foreach (const QSerialPortInfo &serialPortInfo, QSerialPortInfo::availablePorts()) {
        if (serialPortInfo.systemLocation() == m_serialPortName) {
            serialPortFound = true;
            break;
        }
    }

    // Prevent repeating warnings...
    if (!serialPortFound) {
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start();
        }
        return;
    }


    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        qCWarning(dcOwlet()) << "Could not open serial port on" << m_serialPortName << m_serialPort->errorString();
        m_reconnectTimer->start();
        return;
    }

    m_reconnectTimer->stop();
    emit connectedChanged(true);
}

void OwletSerialTransport::disconnectTransport()
{
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
        emit connectedChanged(false);
    }
}

void OwletSerialTransport::onReadyRead()
{
    QByteArray data = m_serialPort->readAll();
    qCDebug(dcOwlet()) << "UART <-- raw:" << data.toHex() << qUtf8Printable(data);

    for (int i = 0; i < data.length(); i++) {
        quint8 receivedByte = data.at(i);

        if (m_protocolEscaping) {
            switch (receivedByte) {
            case SlipProtocolTransposedEnd:
                m_buffer.append(static_cast<quint8>(SlipProtocolEnd));
                m_protocolEscaping = false;
                break;
            case SlipProtocolTransposedEsc:
                m_buffer.append(static_cast<quint8>(SlipProtocolEsc));
                m_protocolEscaping = false;
                break;
            default:
                // SLIP protocol violation...received escape, but it is not an escaped byte
                break;
            }
        }

        switch (receivedByte) {
        case SlipProtocolEnd:
            // We are done with this package, process it and reset the buffer
            if (!m_buffer.isEmpty() && m_buffer.length() >= 3) {
                qCDebug(dcOwlet()) << "UART <--" << m_buffer.toHex() << qUtf8Printable(m_buffer);
                emit dataReceived(m_buffer);
            }
            m_buffer.clear();
            m_protocolEscaping = false;
            break;
        case SlipProtocolEsc:
            // The next byte will be escaped, lets wait for it
            m_protocolEscaping = true;
            break;
        default:
            // Nothing special, just add to buffer
            m_buffer.append(receivedByte);
            break;
        }
    }
}
