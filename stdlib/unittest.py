# Lifecycle and assertion behavior adapted from CPython Lib/unittest/case.py
# at v3.14.5 and v3.14.10. Copyright (c) Python Software Foundation.
# Adapted portions are licensed under the PSF License Agreement.

"""Explicitly runnable unittest subset for CloverVM.

Use Case("test_name").run() or TestSuite([...]).run(result). Discovery,
callable case/suite instances, runners, skips, cleanups, and context-manager
assertRaises are not supported. Test methods must be ordinary Python methods;
attribute reads do not yet produce bound-method callback objects. Failure/error
entries are (test, exception) pairs, without traceback formatting. successes is
a CloverVM extension.

Assertions use minimal messages: failures pass msg unchanged to fail(), even
for invalid container arguments. There are no generated diffs, longMessage or
maxDiff handling. Typed list/tuple equality shares a private helper; generic
assertSequenceEqual is not provided.

Subclass and attribute assertions use the existing issubclass/hasattr subset:
stored classes, class tuples and stored attributes, without subclass or
attribute-lookup hooks or descriptor execution. As in issubclass, nonclass
left operands are rejected even with an empty superclass tuple. Diagnostic-only
tuple rescans, repr calls and attribute reads are omitted.
"""


class TestResult:
    def __init__(self):
        self.testsRun = 0
        self.successes = []
        self.failures = []
        self.errors = []

    def startTest(self, test):
        self.testsRun += 1

    def stopTest(self, test):
        pass

    def addSuccess(self, test):
        self.successes.append(test)

    def addFailure(self, test, error):
        self.failures.append((test, error))

    def addError(self, test, error):
        self.errors.append((test, error))

    def wasSuccessful(self):
        return not self.failures and not self.errors


class TestCase:
    failureException = AssertionError

    def __init__(self, methodName):
        # Validate the explicitly supplied name at construction, like CPython.
        try:
            getattr(self, methodName)
        except AttributeError:
            raise ValueError("no such test method: " + methodName)
        self._testMethodName = methodName

    def setUp(self):
        pass

    def tearDown(self):
        pass

    def run(self, result=None):
        if result is None:
            result = TestResult()
        result.startTest(self)
        try:
            method = getattr(self, self._testMethodName)
            try:
                self.setUp()
            except KeyboardInterrupt:
                raise
            except BaseException as error:
                self._recordException(result, error)
                return result
            successful = True
            try:
                # Attribute reads currently yield an unbound Python function.
                method(self)
            except KeyboardInterrupt:
                raise
            except BaseException as error:
                successful = False
                self._recordException(result, error)
            try:
                self.tearDown()
            except KeyboardInterrupt:
                raise
            except BaseException as error:
                successful = False
                self._recordException(result, error)
            if successful:
                result.addSuccess(self)
            return result
        finally:
            result.stopTest(self)

    def _recordException(self, result, error):
        if isinstance(error, self.failureException):
            result.addFailure(self, error)
        else:
            result.addError(self, error)

    def fail(self, msg=None):
        raise self.failureException(msg)

    def assertTrue(self, expr, msg=None):
        if not expr:
            self.fail(msg)

    def assertFalse(self, expr, msg=None):
        if expr:
            self.fail(msg)

    def assertEqual(self, first, second, msg=None):
        if not first == second:
            self.fail(msg)

    def assertNotEqual(self, first, second, msg=None):
        if not first != second:
            self.fail(msg)

    def assertLess(self, a, b, msg=None):
        if not a < b:
            self.fail(msg)

    def assertLessEqual(self, a, b, msg=None):
        if not a <= b:
            self.fail(msg)

    def assertGreater(self, a, b, msg=None):
        if not a > b:
            self.fail(msg)

    def assertGreaterEqual(self, a, b, msg=None):
        if not a >= b:
            self.fail(msg)

    def _assertTypedSequenceEqual(self, seq1, seq2, seq_type, msg):
        if not isinstance(seq1, seq_type):
            self.fail(msg)
        if not isinstance(seq2, seq_type):
            self.fail(msg)
        # Like CPython, check lengths before invoking sequence equality.
        # Rich diagnostic indexing and formatting are outside this subset.
        try:
            len(seq1)
            len(seq2)
        except TypeError:
            self.fail(msg)
            return
        except NotImplementedError:
            self.fail(msg)
            return
        if not seq1 == seq2:
            self.fail(msg)

    def assertListEqual(self, list1, list2, msg=None):
        self._assertTypedSequenceEqual(list1, list2, list, msg)

    def assertTupleEqual(self, tuple1, tuple2, msg=None):
        self._assertTypedSequenceEqual(tuple1, tuple2, tuple, msg)

    def assertDictEqual(self, d1, d2, msg=None):
        if not isinstance(d1, dict):
            self.fail(msg)
        if not isinstance(d2, dict):
            self.fail(msg)
        if d1 != d2:
            self.fail(msg)

    def assertIs(self, first, second, msg=None):
        if first is not second:
            self.fail(msg)

    def assertIsNot(self, first, second, msg=None):
        if first is second:
            self.fail(msg)

    def assertIsNone(self, obj, msg=None):
        if obj is not None:
            self.fail(msg)

    def assertIsNotNone(self, obj, msg=None):
        if obj is None:
            self.fail(msg)

    def assertIn(self, member, container, msg=None):
        if member not in container:
            self.fail(msg)

    def assertNotIn(self, member, container, msg=None):
        if member in container:
            self.fail(msg)

    def assertIsInstance(self, obj, cls, msg=None):
        if not isinstance(obj, cls):
            self.fail(msg)

    def assertNotIsInstance(self, obj, cls, msg=None):
        if isinstance(obj, cls):
            self.fail(msg)

    def assertIsSubclass(self, cls, superclass, msg=None):
        try:
            if issubclass(cls, superclass):
                return
        except TypeError:
            if not isinstance(cls, type):
                self.fail(msg)
            raise
        self.fail(msg)

    def assertNotIsSubclass(self, cls, superclass, msg=None):
        try:
            if not issubclass(cls, superclass):
                return
        except TypeError:
            if not isinstance(cls, type):
                self.fail(msg)
            raise
        self.fail(msg)

    def assertHasAttr(self, obj, name, msg=None):
        if not hasattr(obj, name):
            self.fail(msg)

    def assertNotHasAttr(self, obj, name, msg=None):
        if hasattr(obj, name):
            self.fail(msg)

    def assertRaises(self, expected_exception, *args, **kwargs):
        # Validate before calling the function, including the no-exception path.
        if isinstance(expected_exception, tuple):
            for exception in expected_exception:
                if not issubclass(exception, BaseException):
                    raise TypeError("expected an exception class")
        elif not issubclass(expected_exception, BaseException):
            raise TypeError("expected an exception class")
        if not args:
            raise TypeError("assertRaises requires a callable")
        # Extract the callable without reserving any of its keyword names.
        function = args[0]
        args = args[1:]
        try:
            function(*args, **kwargs)
        except BaseException as error:
            if isinstance(error, expected_exception):
                return None
            raise
        self.fail("expected exception was not raised")


class TestSuite:
    def __init__(self, tests=()):
        self._tests = []
        self.addTests(tests)

    def addTest(self, test):
        self._tests.append(test)

    def addTests(self, tests):
        for test in tests:
            self.addTest(test)

    def run(self, result):
        for test in self._tests:
            test.run(result)
        return result
