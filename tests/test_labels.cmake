# All discovered tests already carry full; regular is the nested subset.
if(clovervm_regular_tests)
	set_tests_properties(${clovervm_regular_tests}
		PROPERTIES LABELS "regular;full")
endif()
