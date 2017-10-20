

#include "common.h"

#include <time.h>

#include "mmapGpio.hpp"

#include "NXGeom.hpp"
#include "NXCanvas.hpp"
#include "KBScreen.hpp"

#include "NXCStr.hpp"
#include "NXFilePath.hpp"
#include "NXUnixPacketSocket.hpp"

void wait_for_kbgui(NXUnixPacketSocket &listen_sock, NXUnixPacketSocket &kbgui_sock);
bool wait_for_kbreq(NXUnixPacketSocket &listen_sock, NXUnixPacketSocket &kbreq_sock);

int DEBUG = 0;

int main (int argc, char *argv[])
{
    if (getenv("DEBUG"))
        DEBUG = true;

    mmapGpio _rpiGpio;

    {
        // Initialize Buttons
        _rpiGpio.setPinDir(17, mmapGpio::INPUT);
        _rpiGpio.setPinPUD(17, mmapGpio::PUD_UP);

        _rpiGpio.setPinDir(22, mmapGpio::INPUT);
        _rpiGpio.setPinPUD(22, mmapGpio::PUD_UP);

        _rpiGpio.setPinDir(23, mmapGpio::INPUT);
        _rpiGpio.setPinPUD(23, mmapGpio::PUD_UP);

        _rpiGpio.setPinDir(27, mmapGpio::INPUT);
        _rpiGpio.setPinPUD(27, mmapGpio::PUD_UP);

        // Piezo 
        _rpiGpio.setPinDir(13, mmapGpio::OUTPUT);

        // TODO: PWM on 18 for backlight
    }

    KBScreen screen;

    // Run Loop
    //

    // Open a unix domain socket
    auto listen_sock = NXUnixPacketSocket::CreateServer("/tmp/kb-gpio");

    NXUnixPacketSocket kbgui_sock;
    bool               kbgui_recvd_wait;

    NXUnixPacketSocket req_sock;
    time_t             req_timer;
    bool               req_recvd_wait;

    int  button        = -1;
    bool sleep_mode    = false;
    int  sleep_counter = 0;

    const char * button_msgs[] = { "b0", "b1", "b2", "b3" };


    listen_sock.listen();

    wait_for_kbgui(listen_sock, kbgui_sock);
    kbgui_sock.send_msg("ack");
    kbgui_recvd_wait = false;

    while (true)
    {

        while(!sleep_mode)
        {
            // Check listen_sock for new connections
            // 
            // Only when we don't have a valid request socket
            if ((!req_sock.valid()) && listen_sock.readable())
            {
                D("Getting new req_sock");

                if (wait_for_kbreq(listen_sock, req_sock))
                {
                    D("Sending new req_sock ack");
                    req_sock.send_msg("ack");
                    req_timer = 0;
                    req_recvd_wait = false;
                }
            }

            // If we have a valid alert/request
            // 
            // Since the kb-gui is waiting on events, we can
            // hand the screen and events to the new connection
            //
            if (req_sock.valid())
            {
                while (req_sock.readable())
                {
                    auto msg = req_sock.recv_msg();

                    fprintf(stderr, "Got req  msg %s\n", msg._str);

                    // Process message
                    if (msg == "")
                    {
                        D("req_sock closed");
                        // Close down socket
                        req_sock.reset();

                        kbgui_sock.send_msg("wake");
                        {
                            auto msg = kbgui_sock.recv_msg();

                            fprintf(stderr, "Wake kbgui msg %s\n", msg._str);

                            // Process message
                            if (msg == "")
                            {
                                fprintf(stderr, "Got kbgui connection died.\n");

                                wait_for_kbgui(listen_sock, kbgui_sock);
                                kbgui_sock.send_msg("ack");
                                kbgui_recvd_wait = false;
                            }
                            else
                            if (msg == "wait")
                            {
                                kbgui_recvd_wait = true;
                            }
                        }
                    }
                    else
                    if (msg == "sysbeep")
                    {
                        _rpiGpio.play_tone(13, 6000, 50);
                        _rpiGpio.play_tone(13, 7000, 50);
                        _rpiGpio.play_tone(13, 8000, 50);
                    }
                    else
                    if (msg == "delaysleep")
                    {
                        sleep_counter = -300;   // Boost it 30 seconds
                    }
                    else
                    if (msg == "wait")
                    {
                        req_recvd_wait = true;
                    }
                }

                // Client has handed back control
                if (req_recvd_wait)
                {
                    if (time(NULL) - req_timer)
                    {
                        D("gpio sent tick to req");
                        req_timer = time(NULL);
                        req_sock.send_msg("tick");
                        req_recvd_wait = false;
                    }

                    if (button != -1)
                    {
                        D("gpio sent button to req");
                        req_sock.send_msg(button_msgs[button]);
                        req_recvd_wait = false;
                        button = -1;
                    }
                }
            }
            else // kb-gui
            {
                if (kbgui_sock.readable())
                {
                    auto msg = kbgui_sock.recv_msg();

                    // Process message
                    fprintf(stderr, "Got kbgui msg %s\n", msg._str);

                    // Process message
                    if (msg == "")
                    {
                        fprintf(stderr, "Got kbgui connection died.\n");

                        wait_for_kbgui(listen_sock, kbgui_sock);
                        kbgui_sock.send_msg("ack");
                        kbgui_recvd_wait = false;
                    }
                    else
                    if (msg == "wait")
                    {
                        D("kb-gui sent wait");
                        kbgui_recvd_wait = true;
                    }
                }

                // Client has handed back control
                if (kbgui_recvd_wait)
                {
                    if (button != -1)
                    {
                        D("gpio sent button to kbgui");
                        kbgui_sock.send_msg(button_msgs[button]);
                        kbgui_recvd_wait = false;
                        button = -1;
                    }
                }
            }

            // Check buttons

            if (_rpiGpio.readPin(17) == mmapGpio::LOW)
                button = 0;
            else
            if (_rpiGpio.readPin(22) == mmapGpio::LOW)
                button = 1;
            else
            if (_rpiGpio.readPin(23) == mmapGpio::LOW)
                button = 2;
            else
            if (_rpiGpio.readPin(27) == mmapGpio::LOW)
                button = 3;


            if (button == -1)
            {
                sleep_counter++;
                if (sleep_counter > 150)    // 150 * 0.1 sec = 15 seconds
                {
                    sleep_mode = true;
                    break;
                }
            }
            else
            {
                sleep_counter = 0;
                fprintf(stderr, "Button! %d\n", button);

                // Wait for all to go to HIGH
                // Ghetto debouncing...
                while (true)
                {
                    usleep(100000); //delay for 0.1 seconds
                    if ( true
                            && (_rpiGpio.readPin(17) == mmapGpio::HIGH)
                            && (_rpiGpio.readPin(22) == mmapGpio::HIGH)
                            && (_rpiGpio.readPin(23) == mmapGpio::HIGH)
                            && (_rpiGpio.readPin(27) == mmapGpio::HIGH)
                       )
                        break;
                }
            }
            usleep(100000); //delay for 0.1 seconds
        }

        // Blank the screen
        screen.canvas()->clear();
        screen.flush();

        // TODO: set backlight pwm to zero

        fprintf(stderr, "sleep mode = %d\n", sleep_mode);
        while (sleep_mode)
        {
            usleep(100000); //delay for 0.1 seconds

            // If any key is hit
            if ( false
                || (_rpiGpio.readPin(17) == mmapGpio::LOW)
                || (_rpiGpio.readPin(22) == mmapGpio::LOW)
                || (_rpiGpio.readPin(23) == mmapGpio::LOW)
                || (_rpiGpio.readPin(27) == mmapGpio::LOW)
                || listen_sock.readable()
           )
            sleep_mode = false;
            sleep_counter = 0;
        }
        // Slight debounce
        usleep(200000); //delay for 0.2 seconds

        if (DEBUG && listen_sock.readable())
            fprintf(stderr, "gpio woke up because listen readable\n");

        // Block until main gui draws
        // Should optimize this later for the 'req' case
        kbgui_sock.send_msg("wake");
        {
            auto msg = kbgui_sock.recv_msg();

            fprintf(stderr, "Last wake kbgui msg %s\n", msg._str);

            // Process message
            if (msg == "")
            {
                fprintf(stderr, "Got kbgui connection died.\n");

                wait_for_kbgui(listen_sock, kbgui_sock);
                kbgui_sock.send_msg("ack");
                kbgui_recvd_wait = false;
            }
            else
            if (msg == "wait")
            {
                kbgui_recvd_wait = true;
            }
        }
    }
}

void wait_for_kbgui(NXUnixPacketSocket &listen_sock, NXUnixPacketSocket &kbgui_sock)
{
    while (true)
    {
        // All of these calls block
        auto new_sock = listen_sock.accept();
        auto msg = new_sock.recv_msg();

        if (msg == "kb-gui")
        {
            // SUCCESS

            // crosstool-NG not working for me here
            // kbgui_sock = std::move(new_sock);

            kbgui_sock.reset();
            kbgui_sock._fd     = new_sock.release();
            kbgui_sock._listen = false;

            return;
        }
        else
        if (msg == "kb-req")
        {
            fprintf(stderr, "Strange - got a kb-req before kb-gui established\n");
            new_sock.send_msg("retry");
        }
        else
        if (msg == "")
        {
            fprintf(stderr, "Strange - the other side crashed\n");
        }
        else
            panic1("Unknown mesg");
    }
}

bool wait_for_kbreq(NXUnixPacketSocket &listen_sock, NXUnixPacketSocket &kbreq_sock)
{
    {
        // All of these calls block
        auto new_sock = listen_sock.accept();
        auto msg = new_sock.recv_msg();

        if (msg == "kb-req")
        {
            // SUCCESS

            // crosstool-NG not working for me here
            // kbreq_sock = std::move(new_sock);

            kbreq_sock.reset();
            kbreq_sock._fd     = new_sock.release();
            kbreq_sock._listen = false;

            return true;
        }
        else
        if (msg == "kb-gui")
        {
            fprintf(stderr, "Strange - got a kb-gui before kb-gui established\n");
            panic1("kb-gui before kb-req");
        }
        else
        if (msg == "")
        {
            fprintf(stderr, "Strange - the other side crashed\n");
        }
        else
            panic1("Unknown mesg");
    }

    // Either we had SUCCESS or an EOF
    return false;
}

