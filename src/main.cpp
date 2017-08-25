

#include "common.h"

#include "mmapGpio.hpp"

#include "NXGeom.hpp"
#include "NXCanvas.hpp"

#include "NXCStr.hpp"
#include "NXFilePath.hpp"
#include "NXUnixPacketSocket.hpp"

void wait_for_kbgui(NXUnixPacketSocket &listen_sock, NXUnixPacketSocket &kbgui_sock);
bool wait_for_kbreq(NXUnixPacketSocket &listen_sock, NXUnixPacketSocket &kbreq_sock);

main()
{
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

    // Initialize render to /dev/fb1
    //
    int fbfd = open("/dev/fb1", O_RDWR);

    NXRect screen_rect = {0, 0, 320, 240};
    int screen_datasize = screen_rect.size.w * screen_rect.size.h * 2;
    void * fbp = mmap(0, screen_datasize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    close(fbfd);
    NXCanvas screen { NXBitmap{(uint8_t *)fbp, screen_rect, NXColorChan::RGB565} };


    // Run Loop
    //

    // Open a unix domain socket
    auto listen_sock = NXUnixPacketSocket::CreateServer("/tmp/kb-gpio");

    NXUnixPacketSocket kbgui_sock;

    NXUnixPacketSocket req_sock;

    int button = -1;
    bool sleep_mode = false;
    U16  sleep_counter = 0;

    const char * button_msgs[] = { "b0", "b1", "b2", "b3" };


    listen_sock.listen();

    wait_for_kbgui(listen_sock, kbgui_sock);
    kbgui_sock.send_msg("ack");

    while (true)
    {

        while(!sleep_mode)
        {
            // Check listen_sock for new connections
            // 
            // Only when we don't have a valid request socket
            if ((!req_sock.valid()) && listen_sock.readable())
            {
                if (wait_for_kbreq(listen_sock, req_sock))
                {
                    req_sock.send_msg("ack");
                }
            }

            // If we have a valid alert/request
            // 
            // Since the kb-gui is waiting on events, we can
            // hand the screen and events to the new connection
            //
            if (req_sock.valid())
            {
                if (button != -1)
                {
                    req_sock.send_msg(button_msgs[button]);
                    req_sock.recv_ack();
                    button = -1;
                }
                if (req_sock.readable())
                {
                    auto msg = req_sock.recv_msg();

                    printf("Got req  msg %s\n", msg._str);

                    // Process message
                    if (msg == "")
                    {
                        // Close down socket
                        req_sock.reset();

                        kbgui_sock.send_msg("wake");
                        kbgui_sock.recv_ack();
                    }
                }
            }
            else // kb-gui
            {
                if (button != -1)
                {
                    kbgui_sock.send_msg(button_msgs[button]);
                    kbgui_sock.recv_ack();
                    button = -1;
                }
                if (kbgui_sock.readable())
                {
                    auto msg = kbgui_sock.recv_msg();

                    // Process message
                    printf("Got kbgui msg %s\n", msg._str);

                    // Process message
                    if (msg == "")
                    {
                        printf("Got kbgui connection died.\n");

                        wait_for_kbgui(listen_sock, kbgui_sock);
                        kbgui_sock.send_msg("ack");
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
                if (sleep_counter > 100)
                {
                    sleep_mode = true;
                    break;
                }
            }
            else
            {
                sleep_counter = 0;
                printf("Button! %d\n", button);

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
        screen.fill_rect(&screen_rect, NXColor{0,0,0,1});
        // TODO: set backlight pwm to zero

        printf("sleep mode = %d", sleep_mode);
        while (sleep_mode)
        {
            usleep(100000); //delay for 0.1 seconds

            // If any key is hit
            if ( false
                || (_rpiGpio.readPin(17) == mmapGpio::LOW)
                || (_rpiGpio.readPin(22) == mmapGpio::LOW)
                || (_rpiGpio.readPin(23) == mmapGpio::LOW)
                || (_rpiGpio.readPin(27) == mmapGpio::LOW)
           )
            sleep_mode = false;
            sleep_counter = 0;
        }
        // Slight debounce
        usleep(200000); //delay for 0.2 seconds

        kbgui_sock.send_msg("wake");
        kbgui_sock.recv_ack();
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
            printf("Strange - got a kb-req before kb-gui established\n");
            new_sock.send_msg("retry");
        }
        else
        if (msg == "")
        {
            printf("Strange - the other side crashed\n");
        }
        else
            panic();        // Unknown mesg
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
            printf("Strange - got a kb-gui before kb-gui established\n");
            panic();
        }
        else
        if (msg == "")
        {
            printf("Strange - the other side crashed\n");

        }
        else
            panic();        // Unknown mesg
    }

    // Either we had SUCCESS or an EOF
    return false;
}

