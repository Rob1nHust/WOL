#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/ethernet.h>

#include "botlib.h"
#include "wol.h"

/* For each bot command, private message or group message (but this only works
 * if the bot is set as group admin), this function is called in a new thread,
 * with its private state, sqlite database handle and so forth.
 *
 * For group messages, this function is ONLY called if one of the patterns
 * specified as "triggers" in startBot() matched the message. Otherwise we
 * would spawn threads too often :) */
void handleRequest(sqlite3 *dbhandle, BotRequest *br)
{
    // Only process commands that start with "/"
    if (br->request[0] != '/')
    {
        return;
    }

    // Handle set command: /set pc-name ip mac
    if (br->argc == 4 && !strcasecmp(br->argv[0], "/set"))
    {
        char value[256];
        // IP/MAC format
        snprintf(value, sizeof(value), "%s/%s", br->argv[2], br->argv[3]);
        kvSet(dbhandle, br->argv[1], value, 0);
        botSendMessage(br->target, "Set done for PC!", br->msg_id);
        return;
    }

    // Handle get command: /get key
    if (br->argc == 2 && !strcasecmp(br->argv[0], "/get"))
    {
        sds res = kvGet(dbhandle, br->argv[1]);
        if (res)
        {
            botSendMessage(br->target, res, br->msg_id);
        }
        else
        {
            botSendMessage(br->target, "Key not found.", br->msg_id);
        }
        sdsfree(res);
        return;
    }

    // Handle del command: /del key
    if (br->argc == 2 && !strcasecmp(br->argv[0], "/del"))
    {
        kvDel(dbhandle, br->argv[1]);
        botSendMessage(br->target, "Deleted PC!", br->msg_id);
        return;
    }

    // Handle help command
    if (br->argc == 1 && !strcasecmp(br->argv[0], "/help"))
    {
        char *help_text = "Available commands:\n"
                          "/set pc-name ip MAC - Format for storing PC details\n"
                          "/get pc-name - Retrieve stored information\n"
                          "/del pc-name - Delete stored information\n"
                          "/wol pc-name - Send Wake-on-LAN packet\n"
                          "/help - Show this help message";
        botSendMessage(br->target, help_text, 0);
        return;
    }

    // handle wol command: /wol pc-name
    if (br->argc == 2 && !strcasecmp(br->argv[0], "/wol"))
    {
        botSendMessage(br->target, "Sending WOL packet...", br->msg_id);
        wol_header_t *currentWOLHeader = (wol_header_t *)malloc(sizeof(wol_header_t));
        strncpy(currentWOLHeader->remote_addr, REMOTE_ADDR, ADDR_LEN);
        // get the mac address and ip address from the database
        sds res = kvGet(dbhandle, br->argv[1]);
        if (res)
        {
            char *mac = strchr(res, '/');
            if (mac)
            {
                // fill the remote address with null first
                memset(currentWOLHeader->remote_addr, 0, ADDR_LEN);
                strncpy(currentWOLHeader->remote_addr, res, mac - res);
                mac++; // Skip the '/'
                currentWOLHeader->mac_addr = (mac_addr_t *)malloc(sizeof(mac_addr_t));
                if (packMacAddr(mac, currentWOLHeader->mac_addr) == 0)
                {
                    int sock;
                    char message[256];
                    if ((sock = startupSocket()) < 0)
                    {
                        // send error message
                        snprintf(message, sizeof(message), "Error occured during startupSocket()\n");
                    }

                    if (sendWOL(currentWOLHeader, sock) < 0)
                    {
                        snprintf(message, sizeof(message), "Error occured during WOL mac address: %s\n", currentWOLHeader->mac_addr->mac_addr_str);
                    }
                    else
                    {
                        snprintf(message, sizeof(message), "WOL packet sent successfully to %s with IP: %s and MAC: %s!", br->argv[1], currentWOLHeader->remote_addr, currentWOLHeader->mac_addr->mac_addr_str);
                    }
                    botSendMessage(br->target, message, br->msg_id);
                    close(sock);
                }
                free(currentWOLHeader->mac_addr);
            }
        }
        else
        {
            botSendMessage(br->target, "PC not found.", br->msg_id);
        }
        free(currentWOLHeader);
        return;
    }

    // Handle list command: /list
    if (br->argc == 1 && !strcasecmp(br->argv[0], "/list"))
    {
        botSendMessage(br->target, "Listing all PCs...", br->msg_id);
        return;
    }

    // If no valid command was found
    botSendMessage(br->target, "Unknown command. Type /help for available commands.", 0);
}

// This is just called every 1 or 2 seconds. */
void cron(sqlite3 *dbhandle)
{
    UNUSED(dbhandle);
}

int main(int argc, char **argv)
{
    // Only trigger on messages starting with "/"
    static char *triggers[] = {
        "/*",
        NULL,
    };
    startBot(TB_CREATE_KV_STORE, argc, argv, TB_FLAGS_NONE, handleRequest, cron, triggers);
    return 0; /* Never reached. */
}
