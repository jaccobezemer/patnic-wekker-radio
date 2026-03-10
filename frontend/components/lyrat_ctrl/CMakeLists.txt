idf_component_register(SRCS "lyrat_ctrl.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver wekker_protocol
                    )
