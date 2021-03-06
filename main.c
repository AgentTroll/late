#include <stdio.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>
#include <stdlib.h>

#define L_CTRL 37
#define L_SHIFT 50

#define Z 52
#define X 53

#define TRUE 1
#define FALSE 0

#define DISABLED 0
#define HOLD 1
#define CLICK 2

#define PAUSE_ITER 500

const uint32_t enterEventMask[] = {XCB_EVENT_MASK_FOCUS_CHANGE};
const uint32_t eventMasks[] = {XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE};

void openWindow(xcb_connection_t *connection, xcb_window_t window) {
    xcb_query_tree_cookie_t queryCookie = xcb_query_tree(connection, window);
    xcb_query_tree_reply_t *queryReply = xcb_query_tree_reply(connection, queryCookie, NULL);
    if (queryReply == NULL) {
        return;
    }

    int length = xcb_query_tree_children_length(queryReply);
    xcb_window_t *children = xcb_query_tree_children(queryReply);
    for (int i = 0; i < length; i++) {
        xcb_window_t childWindow = children[i];
        openWindow(connection, childWindow);
    }

    printf("Adding enter event to %d\n", window);
    xcb_change_window_attributes(connection, window, XCB_CW_EVENT_MASK, enterEventMask);

    free(queryReply);
}

void refocus(xcb_connection_t *connection, xcb_window_t *previousWindow, xcb_window_t newWindow) {
    if (*previousWindow == newWindow) {
        return;
    }
    if (*previousWindow != XCB_NONE) {
        xcb_change_window_attributes(connection, *previousWindow, XCB_CW_EVENT_MASK, enterEventMask);
    }
    if (newWindow != XCB_NONE) {
        xcb_change_window_attributes(connection, newWindow, XCB_CW_EVENT_MASK, eventMasks);
    }
    xcb_flush(connection);

    *previousWindow = newWindow;
}

int cleanup(xcb_connection_t *connection) {
    xcb_void_cookie_t cookie = xcb_test_fake_input(connection, XCB_BUTTON_RELEASE, XCB_BUTTON_INDEX_1, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_request_check(connection, cookie);

    xcb_disconnect(connection);

    return 0;
}

int main() {
    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) {
        fprintf(stderr, "Failed to connect to display\n");
        return 1;
    }

    // Listen to focus events on other windows
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(xcb_get_setup(connection));
    openWindow(connection, iterator.data->root);
    xcb_flush(connection);

    // Get current focused window
    xcb_get_input_focus_cookie_t focusCookie = xcb_get_input_focus(connection);
    xcb_get_input_focus_reply_t *focusReply = xcb_get_input_focus_reply(connection, focusCookie, NULL);
    if (focusReply == NULL) {
        return 1;
    }

    xcb_window_t window = XCB_NONE;
    refocus(connection, &window, focusReply->focus);
    openWindow(connection, focusReply->focus);

    free(focusReply);

    int lCtrlPressed = FALSE;
    int lShiftPressed = FALSE;
    int mode = DISABLED;
    while (1) {
        if (mode == CLICK) {
            uint8_t state = XCB_BUTTON_PRESS;
            while (1) {
                xcb_query_pointer_cookie_t pointerCookie = xcb_query_pointer(connection, window);
                xcb_query_pointer_reply_t *pointerReply = xcb_query_pointer_reply(connection,
                                                                                  pointerCookie, NULL);
                xcb_void_cookie_t cookie = xcb_test_fake_input(connection, state, XCB_BUTTON_INDEX_1, XCB_CURRENT_TIME, window, pointerReply->root_x, pointerReply->root_y, 0);
                xcb_request_check(connection, cookie);
                state = state == XCB_BUTTON_PRESS ? XCB_BUTTON_RELEASE : XCB_BUTTON_PRESS;

                xcb_generic_event_t *queuedEvent = NULL;
                for (int i = 0; i < PAUSE_ITER; i++) {
                    queuedEvent = xcb_poll_for_queued_event(connection);
                    if (queuedEvent != NULL) {
                        break;
                    }
                }

                if (queuedEvent != NULL) {
                    break;
                }
            }
        }

        xcb_generic_event_t *event = xcb_wait_for_event(connection);
        if (event == NULL) {
            if (xcb_connection_has_error(connection)) {
                fprintf(stderr, "IO Error occurred fetching event\n");
            }
            continue;
        }

        switch (event->response_type & ~0x80) {
            case XCB_FOCUS_IN: {
                xcb_focus_in_event_t *focusInEvent = (xcb_focus_in_event_t *) event;
                refocus(connection, &window, focusInEvent->event);
                printf("Focus %d\n", window);
                break;
            }
            case XCB_KEY_PRESS: {
                xcb_key_press_event_t *pressEvent = (xcb_key_press_event_t *) event;
                xcb_keycode_t keycode = pressEvent->detail;

                switch (keycode) {
                    case L_CTRL:
                        lCtrlPressed = TRUE;
                        break;
                    case L_SHIFT:
                        lShiftPressed = TRUE;
                        break;
                    case Z:
                        if (lCtrlPressed && lShiftPressed) {
                            if (mode == DISABLED) {
                                printf("Toggled to HOLD mode\n");
                                xcb_query_pointer_cookie_t pointerCookie = xcb_query_pointer(connection, window);
                                xcb_query_pointer_reply_t *pointerReply = xcb_query_pointer_reply(connection,
                                                                                                  pointerCookie, NULL);

                                xcb_void_cookie_t cookie = xcb_test_fake_input(connection, XCB_BUTTON_PRESS, XCB_BUTTON_INDEX_1, XCB_CURRENT_TIME, window, pointerReply->root_x, pointerReply->root_y, 0);
                                xcb_request_check(connection, cookie);
                                free(pointerReply);
                                mode = HOLD;
                            } else if (mode == HOLD) {
                                printf("Toggled to CLICK mode\n");
                                mode = CLICK;
                            } else {
                                printf("Toggled to DISABLED mode\n");
                                mode = DISABLED;
                            }
                        }
                        break;
                    case X:
                        if (lCtrlPressed && lShiftPressed) {
                            free(event);
                            return cleanup(connection);
                        }
                        break;
                    default:
                        break;
                }
                break;
            }
            case XCB_KEY_RELEASE: {
                xcb_key_press_event_t *pressEvent = (xcb_key_press_event_t *) event;
                xcb_keycode_t keycode = pressEvent->detail;
                switch (keycode) {
                    case L_CTRL:
                        lCtrlPressed = FALSE;
                        break;
                    case L_SHIFT:
                        lShiftPressed = FALSE;
                        break;
                    default:
                        break;
                }
                break;
            }
            default:
                break;
        }

        free(event);
    }
}