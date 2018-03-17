#include "dclhelpers.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

int main()
{
	FLT A[2][4]	= { { 0, 1, 2, 3 }, { 4, 5, 6, 7} };
	FLT B[4][2];
	dclPrint( A[0], 4, 2, 4 );
	dclTransp( B[0], 2, A[0], 4, 2, 4 );
	dclPrint( B[0], 2, 4, 2 );

	int i;
	for( i = 0; i < 8; i++ )
	{
		printf( "%f\n", ((float*)(B[0]))[i] );
	}

	FLT M[3][3] = {
		{ .32, 1, 0 },
		{ 0, 1, 2 },
		{ 1, 0, 1 } };
	FLT Mo[3][3];
	dclInv( Mo[0], 3, M[0], 3, 3 );
	dclPrint( Mo[0], 3, 3, 3 );

	FLT MM[3][3];
	dclMul( MM[0], 3, M[0], 3, Mo[0], 3, 3, 3, 3 );

	printf( "The following should be an identity matrix\n" );
	dclPrint( MM[0], 3, 3, 3 );

	{
		FLT A[3][4];
		dclIdentity(A[0], 4, 3);
		dclPrint(A[0], 4, 3, 4);

		FLT x[4][2] = {
			{7, -7}, {8, -8}, {9, -9}, {10, -10},
		};
		FLT R[4][2];

		printf("%p %p %p\n", A, x, R);
		// dclMul(R, 1, A[0], 4, x, 1, 4, 1, 3);
		dcldgemm(0, 0, 3, 4, 2, 1, A[0], 4, x[0], 2, 0, R[0], 2);

		dclPrint(x[0], 2, 4, 2);
		dclPrint(R[0], 2, 4, 2);

		for (int j = 0; j < 2; j++) {
			for (int i = 0; i < 3; i++)
				assert(R[i][j] == x[i][j]);

			assert(fabs(R[3][j]) < .0000001);
		}
	}
	// void dclTransp( DCL_FLOAT * R, int Rc, const DCL_FLOAT * A, int Ac, int n, int m );

	//	dclIdentity( A[0], MATx, 5 );
	//	dclPrint( A[0], MATx, MATx, MATy );
}

