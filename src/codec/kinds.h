/**
 * @file kinds.h
 * @brief The kinds of the protocol's messages, each named once
 * (R-CONST-2): read in the direction they travel, so `C` from a client is
 * Close and from the server Challenge [PROTOCOL Frames].
 */
#ifndef TRL_CODEC_KINDS_H
#define TRL_CODEC_KINDS_H

#include <stdint.h>

/** @brief Hello [PROTOCOL Hello 'H']. */
#define TRL_KIND_HELLO ((uint8_t)'H')
/** @brief Answer [PROTOCOL Answer 'A']. */
#define TRL_KIND_ANSWER ((uint8_t)'A')
/** @brief Query [PROTOCOL Query 'Q']. */
#define TRL_KIND_QUERY ((uint8_t)'Q')
/** @brief Prepare [PROTOCOL Prepare 'P']. */
#define TRL_KIND_PREPARE ((uint8_t)'P')
/** @brief Execute [PROTOCOL Execute 'E']. */
#define TRL_KIND_EXECUTE ((uint8_t)'E')
/** @brief Fetch [PROTOCOL Fetch 'F']. */
#define TRL_KIND_FETCH ((uint8_t)'F')
/** @brief Close [PROTOCOL Close 'C']. */
#define TRL_KIND_CLOSE ((uint8_t)'C')
/** @brief Deallocate [PROTOCOL Deallocate 'D']. */
#define TRL_KIND_DEALLOCATE ((uint8_t)'D')
/** @brief A Lob request [PROTOCOL Lob 'L']. */
#define TRL_KIND_LOB_REQUEST ((uint8_t)'L')
/** @brief Cancel [PROTOCOL Cancel 'Z']. */
#define TRL_KIND_CANCEL ((uint8_t)'Z')
/** @brief Terminate [PROTOCOL Terminate 'X']. */
#define TRL_KIND_TERMINATE ((uint8_t)'X')
/** @brief Describe [PROTOCOL Describe 'Y']. */
#define TRL_KIND_DESCRIBE ((uint8_t)'Y')
/** @brief Ping [PROTOCOL Ping 'G']. */
#define TRL_KIND_PING ((uint8_t)'G')
/** @brief Reset [PROTOCOL Reset 'W']. */
#define TRL_KIND_RESET ((uint8_t)'W')
/** @brief ChangeUser [PROTOCOL ChangeUser 'U']. */
#define TRL_KIND_CHANGE_USER ((uint8_t)'U')
/** @brief CopyIn [PROTOCOL CopyIn 'I']. */
#define TRL_KIND_COPY_IN ((uint8_t)'I')
/** @brief CopyData [PROTOCOL CopyData 'J']. */
#define TRL_KIND_COPY_DATA ((uint8_t)'J')
/** @brief CopyEnd [PROTOCOL CopyEnd 'N']. */
#define TRL_KIND_COPY_END ((uint8_t)'N')
/** @brief Debug, with `debugger` *(extension)* [PROTOCOL Debug 'V']. */
#define TRL_KIND_DEBUG ((uint8_t)'V')

/** @brief Challenge [PROTOCOL Challenge 'C']. */
#define TRL_KIND_CHALLENGE ((uint8_t)'C')
/** @brief Ready [PROTOCOL Ready 'R']. */
#define TRL_KIND_READY ((uint8_t)'R')
/** @brief Prepared [PROTOCOL Prepared 'P']. */
#define TRL_KIND_PREPARED ((uint8_t)'P')
/** @brief Description [PROTOCOL Description 'D']. */
#define TRL_KIND_DESCRIPTION ((uint8_t)'D')
/** @brief Rows [PROTOCOL Rows 'B']. */
#define TRL_KIND_ROWS ((uint8_t)'B')
/** @brief Suspended [PROTOCOL Suspended 'S']. */
#define TRL_KIND_SUSPENDED ((uint8_t)'S')
/** @brief Complete [PROTOCOL Complete 'K']. */
#define TRL_KIND_COMPLETE ((uint8_t)'K')
/** @brief Error [PROTOCOL Error 'E']. */
#define TRL_KIND_ERROR ((uint8_t)'E')
/** @brief A Lob reply [PROTOCOL Lob 'L']. */
#define TRL_KIND_LOB_REPLY ((uint8_t)'L')
/** @brief Notice [PROTOCOL Notice 'N']. */
#define TRL_KIND_NOTICE ((uint8_t)'N')
/** @brief Status [PROTOCOL Status 'T']. */
#define TRL_KIND_STATUS ((uint8_t)'T')
/** @brief Pong [PROTOCOL Pong 'G']. */
#define TRL_KIND_PONG ((uint8_t)'G')
/** @brief Debugged, with `debugger` *(extension)* [PROTOCOL Debugged 'V']. */
#define TRL_KIND_DEBUGGED ((uint8_t)'V')
/** @brief Event, sent unasked, with `debugger` *(extension)* [PROTOCOL Event 'W']. */
#define TRL_KIND_EVENT ((uint8_t)'W')

/** @brief A compressed frame, either way [PROTOCOL Compressed frames]. */
#define TRL_KIND_COMPRESSED ((uint8_t)'z')

/** @brief A Lob request's op: read [PROTOCOL Lob 'L']. */
#define TRL_LOB_READ 0U
/** @brief A Lob request's op: create. */
#define TRL_LOB_CREATE 1U
/** @brief A Lob request's op: append. */
#define TRL_LOB_APPEND 2U
/** @brief A Lob request's op: free. */
#define TRL_LOB_FREE 3U
/** @brief A Lob request's op: hold. */
#define TRL_LOB_HOLD 4U
/** @brief A Lob reply's what: data. */
#define TRL_LOB_DATA 0U
/** @brief A Lob reply's what: locator. */
#define TRL_LOB_LOCATOR 1U

/** @brief Describe's what: a statement [PROTOCOL Describe 'Y']. */
#define TRL_DESCRIBE_STATEMENT 0U
/** @brief Describe's what: a cursor. */
#define TRL_DESCRIBE_CURSOR 1U

#endif
