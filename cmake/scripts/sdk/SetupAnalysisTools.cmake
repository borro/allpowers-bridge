cmake_minimum_required(VERSION 4.4)
include("${CMAKE_CURRENT_LIST_DIR}/../../modules/sdk/AnalysisTools.cmake")
ap_setup_analysis_tools(WORK "${WORK}" KIND "${KIND}" OUTPUT tool)
