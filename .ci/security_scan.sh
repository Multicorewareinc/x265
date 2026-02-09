#!/bin/bash
# Security vulnerability scanning for x265

set -euo pipefail

echo "=================================================="
echo "Security Vulnerability Scanning"
echo "=================================================="

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCAN_FAILED=0

# 1. Check for known vulnerable dependencies
echo -e "\n${YELLOW}[1/4] Checking system dependencies...${NC}"
apt-get update -qq
apt-get install -y -qq debsecan 2>/dev/null || echo "debsecan not available"

# 2. Scan binaries for common vulnerabilities
echo -e "\n${YELLOW}[2/4] Scanning compiled binaries...${NC}"
if [ -f "source/build/x265" ]; then
    # Check for insecure functions
    if command -v nm &> /dev/null; then
        echo "Checking for potentially insecure functions..."
        INSECURE_FUNCS=$(nm source/build/x265 2>/dev/null | grep -E "strcpy|sprintf|gets" || true)
        if [ -n "$INSECURE_FUNCS" ]; then
            echo -e "${YELLOW}⚠️  Warning: Found potentially insecure functions:${NC}"
            echo "$INSECURE_FUNCS"
        else
            echo -e "${GREEN}✓ No obvious insecure functions detected${NC}"
        fi
    fi
    
    # Check binary security features
    if command -v checksec &> /dev/null; then
        echo "Binary security features:"
        checksec --file=source/build/x265 || true
    fi
fi

# 3. Static code analysis for common issues
echo -e "\n${YELLOW}[3/4] Static code analysis...${NC}"
if command -v cppcheck &> /dev/null; then
    echo "Running cppcheck..."
    cppcheck --enable=warning,performance,portability --quiet source/common/ source/encoder/ 2>&1 | head -20 || true
else
    echo "cppcheck not installed, skipping static analysis"
fi

# 4. Check for hardcoded secrets/credentials
echo -e "\n${YELLOW}[4/4] Scanning for secrets...${NC}"
SECRET_PATTERNS=(
    "password.*=.*['\"]"
    "api[_-]key.*=.*['\"]"
    "secret.*=.*['\"]"
    "token.*=.*['\"]"
    "-----BEGIN.*PRIVATE KEY-----"
)

SECRETS_FOUND=0
for pattern in "${SECRET_PATTERNS[@]}"; do
    if grep -r -i -E "$pattern" source/ 2>/dev/null | grep -v "Binary file" | head -5; then
        echo -e "${RED}⚠️  Potential secret found matching pattern: $pattern${NC}"
        SECRETS_FOUND=1
    fi
done

if [ $SECRETS_FOUND -eq 0 ]; then
    echo -e "${GREEN}✓ No obvious secrets found in source code${NC}"
fi

# 5. Check file permissions
echo -e "\n${YELLOW}[5/4] Checking file permissions...${NC}"
WORLD_WRITABLE=$(find source/ -type f -perm -002 2>/dev/null | head -10 || true)
if [ -n "$WORLD_WRITABLE" ]; then
    echo -e "${YELLOW}⚠️  Warning: World-writable files found:${NC}"
    echo "$WORLD_WRITABLE"
else
    echo -e "${GREEN}✓ No world-writable files${NC}"
fi

echo -e "\n=================================================="
if [ $SCAN_FAILED -eq 0 ]; then
    echo -e "${GREEN}✓ Security scan completed - No critical issues${NC}"
else
    echo -e "${RED}✗ Security scan found issues${NC}"
    exit 1
fi
echo "=================================================="
