#!/usr/bin/env python3
"""Generate JUnit XML report from test results"""
import sys
import xml.etree.ElementTree as ET
from datetime import datetime

def create_junit_report(test_results, output_file="test-results.xml"):
    """
    Create JUnit XML report
    test_results: list of dict with keys: name, status, time, message
    """
    testsuites = ET.Element("testsuites")
    testsuites.set("name", "x265 Test Suite")
    testsuites.set("tests", str(len(test_results)))
    testsuites.set("failures", str(sum(1 for t in test_results if t['status'] == 'failure')))
    testsuites.set("time", str(sum(t.get('time', 0) for t in test_results)))
    
    testsuite = ET.SubElement(testsuites, "testsuite")
    testsuite.set("name", "x265.tests")
    testsuite.set("tests", str(len(test_results)))
    testsuite.set("failures", str(sum(1 for t in test_results if t['status'] == 'failure')))
    testsuite.set("timestamp", datetime.utcnow().isoformat())
    
    for test in test_results:
        testcase = ET.SubElement(testsuite, "testcase")
        testcase.set("name", test['name'])
        testcase.set("classname", f"x265.{test.get('suite', 'tests')}")
        testcase.set("time", str(test.get('time', 0)))
        
        if test['status'] == 'failure':
            failure = ET.SubElement(testcase, "failure")
            failure.set("message", test.get('message', 'Test failed'))
            failure.text = test.get('output', '')
        elif test['status'] == 'skipped':
            ET.SubElement(testcase, "skipped")
    
    tree = ET.ElementTree(testsuites)
    ET.indent(tree, space="  ")
    tree.write(output_file, encoding="utf-8", xml_declaration=True)
    print(f"JUnit report written to {output_file}")

if __name__ == "__main__":
    # Example usage - in practice, parse test output
    example_tests = [
        {"name": "TestBench-correctness", "status": "success", "time": 45.2, "suite": "unit"},
        {"name": "TestBench-pixel", "status": "success", "time": 12.5, "suite": "benchmarks"},
        {"name": "Encoding-Test1-Fast", "status": "success", "time": 8.3, "suite": "encoding"},
        {"name": "Encoding-Test2-Medium", "status": "success", "time": 15.7, "suite": "encoding"},
    ]
    create_junit_report(example_tests)
